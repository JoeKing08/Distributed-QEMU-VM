#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wavevm_canonical.h"
#include "wavevm_control_plane.h"

struct id_provider_context {
    uint8_t next_id;
    uint64_t next_route_scope_id;
};

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "control-plane test: %s\n", message);
        return -1;
    }
    return 0;
}

static int same_transaction(const struct wvm_coordinator_transaction *left,
                            const struct wvm_coordinator_transaction *right)
{
    return memcmp(left->request_id, right->request_id,
                  sizeof(left->request_id)) == 0 &&
           left->vm_id == right->vm_id &&
           left->vm_incarnation == right->vm_incarnation &&
           left->manifest_generation == right->manifest_generation &&
           memcmp(left->admission_tx_id, right->admission_tx_id,
                  sizeof(left->admission_tx_id)) == 0 &&
           memcmp(left->manifest_id, right->manifest_id,
                  sizeof(left->manifest_id)) == 0 &&
           left->route_scope_key.vm_id == right->route_scope_key.vm_id &&
           left->route_scope_key.vm_incarnation ==
               right->route_scope_key.vm_incarnation &&
           left->route_scope_key.route_scope_id ==
               right->route_scope_key.route_scope_id;
}

static int allocate_id16(void *context, enum wvm_coordinator_id_purpose purpose,
                         uint8_t id[WVM_IDENTITY_ID_BYTES], char *error,
                         size_t error_len)
{
    struct id_provider_context *provider = context;

    (void)purpose;
    (void)error;
    (void)error_len;
    memset(id, 0, WVM_IDENTITY_ID_BYTES);
    id[WVM_IDENTITY_ID_BYTES - 1] = provider->next_id++;
    return 0;
}

static int allocate_route_scope_id(void *context, uint64_t *route_scope_id,
                                   char *error, size_t error_len)
{
    struct id_provider_context *provider = context;

    (void)error;
    (void)error_len;
    *route_scope_id = provider->next_route_scope_id++;
    return 0;
}

static void fill_request(struct wvm_vm_request *request, uint8_t request_id,
                         uint32_t requested_vcpus)
{
    memset(request, 0, sizeof(*request));
    request->api_version = WVM_CANONICAL_SCHEMA_V1;
    request->request_id[WVM_IDENTITY_ID_BYTES - 1] = request_id;
    request->requested_vcpus = requested_vcpus;
    request->requested_memory_bytes = 4 * 1024 * 1024;
    request->execution_backend_policy = WVM_MANIFEST_BACKEND_POLICY_REQUIRE_TCG;
    request->accelerator_policy = WVM_MANIFEST_ACCELERATOR_DISABLED;
    request->placement_policy = WVM_MANIFEST_PLACEMENT_SPREAD;
    request->guest_topology_policy = WVM_MANIFEST_GUEST_TOPOLOGY_FLAT;
    request->consistency_policy.dirty_batch_size = 1;
    request->consistency_policy.handoff_commit_policy = 1;
    request->consistency_policy.subscriber_delivery_policy = 1;
    request->consistency_policy.max_commit_latency_ms = 1000;
    memset(request->storage_device_plan.qemu_device_configuration_digest, 0x71,
           sizeof(request->storage_device_plan.qemu_device_configuration_digest));
    request->lifecycle_policy.start_policy = 1;
    request->lifecycle_policy.failure_policy = 1;
    request->lifecycle_policy.completion_query_horizon_ms = 5000;
    request->lifecycle_policy.route_retention_horizon_ms = 6000;
}

int main(void)
{
    char journal_path[] = "/tmp/wavevm-control-plane.XXXXXX";
    struct wvm_control_plane_entry first_entries[4];
    struct wvm_control_plane_entry recovered_entries[4];
    struct wvm_control_plane first_plane;
    struct wvm_control_plane recovered_plane;
    struct wvm_vm_namespace_record first_namespace_records[4];
    struct wvm_vm_namespace_record recovered_namespace_records[4];
    struct wvm_vm_namespace_allocator first_namespace_allocator;
    struct wvm_vm_namespace_allocator recovered_namespace_allocator;
    struct id_provider_context provider_context = {
        .next_id = 1,
        .next_route_scope_id = 1,
    };
    struct wvm_coordinator_id_provider id_provider = {
        .context = &provider_context,
        .allocate_id16 = allocate_id16,
        .allocate_route_scope_id = allocate_route_scope_id,
    };
    struct wvm_vm_request request;
    struct wvm_vm_request changed_request;
    struct wvm_vm_request next_request;
    struct wvm_coordinator_transaction transaction;
    struct wvm_coordinator_transaction replay_transaction;
    struct wvm_coordinator_transaction next_transaction;
    enum wvm_control_plane_submit_result result;
    const struct wvm_control_plane_entry *entry;
    char error[256] = {0};
    int fd;

    fd = mkstemp(journal_path);
    if (fd < 0) {
        perror("mkstemp");
        return 1;
    }
    close(fd);

    wvm_vm_namespace_allocator_init(
        &first_namespace_allocator, first_namespace_records,
        sizeof(first_namespace_records) / sizeof(first_namespace_records[0]),
        1);
    wvm_control_plane_init(&first_plane, first_entries,
                           sizeof(first_entries) / sizeof(first_entries[0]));
    if (expect(wvm_control_plane_open(&first_plane, journal_path,
                                      &first_namespace_allocator, error,
                                      sizeof(error)) == 0,
               "open empty durable control plane")) {
        unlink(journal_path);
        return 1;
    }

    fill_request(&request, 0x42, 2);
    if (expect(wvm_control_plane_begin(
                   &first_plane, &request, &first_namespace_allocator,
                   &id_provider, &result, &transaction, error,
                   sizeof(error)) == 0 &&
                   result == WVM_CONTROL_PLANE_SUBMIT_NEW &&
                   transaction.vm_id == 256 &&
                   transaction.vm_incarnation == 1,
               "persist first request identity exactly once")) {
        wvm_control_plane_close(&first_plane);
        unlink(journal_path);
        return 1;
    }

    if (expect(wvm_control_plane_begin(
                   &first_plane, &request, &first_namespace_allocator,
                   &id_provider, &result, &replay_transaction, error,
                   sizeof(error)) == 0 &&
                   result == WVM_CONTROL_PLANE_SUBMIT_REPLAY &&
                   same_transaction(&transaction, &replay_transaction) &&
                   provider_context.next_id == 3 &&
                   provider_context.next_route_scope_id == 2,
               "replay identical request without allocating another namespace")) {
        wvm_control_plane_close(&first_plane);
        unlink(journal_path);
        return 1;
    }

    changed_request = request;
    changed_request.requested_vcpus++;
    if (expect(wvm_control_plane_begin(
                   &first_plane, &changed_request, &first_namespace_allocator,
                   &id_provider, &result, &replay_transaction, error,
                   sizeof(error)) != 0,
               "reject semantic request-id collision")) {
        wvm_control_plane_close(&first_plane);
        unlink(journal_path);
        return 1;
    }

    if (expect(wvm_control_plane_transition(
                   &first_plane, &transaction,
                   WVM_LIFECYCLE_IDENTITY_ALLOCATED,
                   WVM_LIFECYCLE_ABORTING, error, sizeof(error)) == 0,
               "durably record pre-activation abort progress")) {
        wvm_control_plane_close(&first_plane);
        unlink(journal_path);
        return 1;
    }
    wvm_control_plane_close(&first_plane);

    /* Simulate a crash during a later append: the partial tail is not a result. */
    fd = open(journal_path, O_WRONLY | O_APPEND);
    if (fd < 0 || write(fd, "WVM", 3) != 3) {
        perror("append partial journal");
        if (fd >= 0) {
            close(fd);
        }
        unlink(journal_path);
        return 1;
    }
    close(fd);

    wvm_vm_namespace_allocator_init(
        &recovered_namespace_allocator, recovered_namespace_records,
        sizeof(recovered_namespace_records) /
            sizeof(recovered_namespace_records[0]),
        1);
    wvm_control_plane_init(
        &recovered_plane, recovered_entries,
        sizeof(recovered_entries) / sizeof(recovered_entries[0]));
    if (expect(wvm_control_plane_open(&recovered_plane, journal_path,
                                      &recovered_namespace_allocator, error,
                                      sizeof(error)) == 0,
               "recover journal and discard incomplete trailing frame")) {
        unlink(journal_path);
        return 1;
    }
    entry = wvm_control_plane_find_request(&recovered_plane, request.request_id);
    if (expect(entry != NULL &&
                   entry->transaction.state == WVM_LIFECYCLE_ABORTING &&
                   wvm_vm_namespace_find(&recovered_namespace_allocator, 256) !=
                       NULL,
               "recover request result and namespace allocation")) {
        wvm_control_plane_close(&recovered_plane);
        unlink(journal_path);
        return 1;
    }

    if (expect(wvm_control_plane_begin(
                   &recovered_plane, &request, &recovered_namespace_allocator,
                   &id_provider, &result, &replay_transaction, error,
                   sizeof(error)) == 0 &&
                   result == WVM_CONTROL_PLANE_SUBMIT_REPLAY &&
                   replay_transaction.vm_id == 256,
               "replay original result after restart")) {
        wvm_control_plane_close(&recovered_plane);
        unlink(journal_path);
        return 1;
    }

    fill_request(&next_request, 0x43, 2);
    if (expect(wvm_control_plane_begin(
                   &recovered_plane, &next_request,
                   &recovered_namespace_allocator, &id_provider, &result,
                   &next_transaction, error, sizeof(error)) == 0 &&
                   result == WVM_CONTROL_PLANE_SUBMIT_NEW &&
                   next_transaction.vm_id == 257,
               "recovery prevents VM namespace reuse")) {
        wvm_control_plane_close(&recovered_plane);
        unlink(journal_path);
        return 1;
    }

    wvm_control_plane_close(&recovered_plane);
    unlink(journal_path);
    puts("control-plane durability tests: PASS");
    return 0;
}
