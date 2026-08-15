#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wavevm_control_plane.h"
#include "wavevm_membership.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "control-route-journal test: %s\n", message);
        return -1;
    }
    return 0;
}

static void fill_endpoint(struct wvm_endpoint *endpoint)
{
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->data_transport = WVM_DATA_TRANSPORT_UDP;
    endpoint->data_address_bytes = 4;
    endpoint->data_address[0] = 192;
    endpoint->data_address[1] = 0;
    endpoint->data_address[2] = 2;
    endpoint->data_address[3] = 17;
    endpoint->data_port = 19117;
    endpoint->control_transport = WVM_CONTROL_TRANSPORT_TLS_TCP;
    endpoint->control_port = 19118;
}

static int build_route_transaction(
    struct wvm_route_transaction_record *transaction,
    struct wvm_required_ack_entry required_ack_entries[1], char *error,
    size_t error_len)
{
    struct wvm_required_ack_set ack_set;
    uint8_t ack_bytes[4096];
    size_t ack_byte_count = 0;

    memset(transaction, 0, sizeof(*transaction));
    transaction->operation_id[WVM_IDENTITY_ID_BYTES - 1] = 0x71;
    transaction->route_snapshot_key.scope_key.vm_id = 256;
    transaction->route_snapshot_key.scope_key.vm_incarnation = 1;
    transaction->route_snapshot_key.scope_key.route_scope_id = 1;
    transaction->route_snapshot_key.topology_revision = 4;
    transaction->route_snapshot_key.route_generation = 2;
    memset(transaction->route_snapshot_key.snapshot_digest, 0x71,
           sizeof(transaction->route_snapshot_key.snapshot_digest));

    memset(required_ack_entries, 0, sizeof(*required_ack_entries));
    required_ack_entries[0].member_key.role_type =
        WVM_MANIFEST_ROLE_GATEWAY;
    required_ack_entries[0].member_key.role_id = 17;
    required_ack_entries[0].member_key.instance_id = 1017;
    required_ack_entries[0].role_type = WVM_MANIFEST_ROLE_GATEWAY;
    fill_endpoint(&required_ack_entries[0].endpoint);
    required_ack_entries[0].expected_snapshot_key =
        transaction->route_snapshot_key;

    memset(&ack_set, 0, sizeof(ack_set));
    ack_set.entries.entries = required_ack_entries;
    ack_set.entries.count = 1;
    ack_set.entries.capacity = 1;
    if (wvm_required_ack_set_encode(&ack_set, ack_bytes, sizeof(ack_bytes),
                                    &ack_byte_count, error, error_len) != 0) {
        return -1;
    }

    transaction->required_ack_set.entries.entries = required_ack_entries;
    transaction->required_ack_set.entries.capacity = 1;
    if (wvm_required_ack_set_decode(
            ack_bytes, ack_byte_count, &transaction->required_ack_set, error,
            error_len) != 0) {
        return -1;
    }
    transaction->operation_retention_horizon_ms = 1000;
    transaction->state = WVM_ROUTE_TRANSACTION_PREPARING;
    return 0;
}

int main(void)
{
    char journal_path[] = "/tmp/wavevm-route-journal.XXXXXX";
    struct wvm_control_plane_entry first_entries[1];
    struct wvm_control_plane_entry recovered_entries[1];
    struct wvm_control_plane_route_entry first_route_entries[2];
    struct wvm_control_plane_route_entry recovered_route_entries[2];
    struct wvm_control_plane first_plane;
    struct wvm_control_plane recovered_plane;
    struct wvm_vm_namespace_record first_namespace_records[1];
    struct wvm_vm_namespace_record recovered_namespace_records[1];
    struct wvm_vm_namespace_allocator first_namespace_allocator;
    struct wvm_vm_namespace_allocator recovered_namespace_allocator;
    struct wvm_route_transaction_record transaction;
    struct wvm_required_ack_entry required_ack_entries[1];
    const struct wvm_control_plane_route_entry *entry;
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
    wvm_control_plane_set_route_transaction_entries(
        &first_plane, first_route_entries,
        sizeof(first_route_entries) / sizeof(first_route_entries[0]));
    if (expect(wvm_control_plane_open(&first_plane, journal_path,
                                      &first_namespace_allocator, error,
                                      sizeof(error)) == 0,
               "open route journal") ||
        expect(build_route_transaction(&transaction, required_ack_entries,
                                       error, sizeof(error)) == 0,
               "build route transaction") ||
        expect(wvm_control_plane_record_route_transaction(
                   &first_plane, &transaction, error, sizeof(error)) == 0,
               "persist route prepare transaction") ||
        expect(wvm_control_plane_record_route_transaction(
                   &first_plane, &transaction, error, sizeof(error)) == 0,
               "replay exact prepare transaction")) {
        wvm_control_plane_close(&first_plane);
        unlink(journal_path);
        return 1;
    }

    entry = wvm_control_plane_find_route_transaction(
        &first_plane, transaction.operation_id);
    if (expect(entry != NULL &&
                   entry->state == WVM_ROUTE_TRANSACTION_PREPARING &&
                   entry->record_byte_count != 0,
               "retain prepared route transaction")) {
        wvm_control_plane_close(&first_plane);
        unlink(journal_path);
        return 1;
    }

    transaction.operation_retention_horizon_ms++;
    if (expect(wvm_control_plane_record_route_transaction(
                   &first_plane, &transaction, error, sizeof(error)) != 0,
               "reject operation ID reuse with different route core")) {
        wvm_control_plane_close(&first_plane);
        unlink(journal_path);
        return 1;
    }
    transaction.operation_retention_horizon_ms--;
    transaction.state = WVM_ROUTE_TRANSACTION_RETIRING;
    if (expect(wvm_control_plane_record_route_transaction(
                   &first_plane, &transaction, error, sizeof(error)) != 0,
               "reject route lifecycle state skip")) {
        wvm_control_plane_close(&first_plane);
        unlink(journal_path);
        return 1;
    }

    transaction.state = WVM_ROUTE_TRANSACTION_ACTIVATED;
    if (expect(wvm_control_plane_record_route_transaction(
                   &first_plane, &transaction, error, sizeof(error)) == 0,
               "persist route activation") ||
        expect(wvm_control_plane_record_route_transaction(
                   &first_plane, &transaction, error, sizeof(error)) == 0,
               "replay route activation")) {
        wvm_control_plane_close(&first_plane);
        unlink(journal_path);
        return 1;
    }
    transaction.state = WVM_ROUTE_TRANSACTION_RETIRING;
    if (expect(wvm_control_plane_record_route_transaction(
                   &first_plane, &transaction, error, sizeof(error)) == 0,
               "persist route retirement start")) {
        wvm_control_plane_close(&first_plane);
        unlink(journal_path);
        return 1;
    }
    transaction.state = WVM_ROUTE_TRANSACTION_RETIRED;
    if (expect(wvm_control_plane_record_route_transaction(
                   &first_plane, &transaction, error, sizeof(error)) == 0,
               "persist route retirement completion")) {
        wvm_control_plane_close(&first_plane);
        unlink(journal_path);
        return 1;
    }
    wvm_control_plane_close(&first_plane);

    /* A torn tail was never fsync-complete and must not alter recovered state. */
    fd = open(journal_path, O_WRONLY | O_APPEND);
    if (fd < 0 || write(fd, "WVM", 3) != 3) {
        perror("append partial route journal");
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
    wvm_control_plane_set_route_transaction_entries(
        &recovered_plane, recovered_route_entries,
        sizeof(recovered_route_entries) / sizeof(recovered_route_entries[0]));
    if (expect(wvm_control_plane_open(&recovered_plane, journal_path,
                                      &recovered_namespace_allocator, error,
                                      sizeof(error)) == 0,
               "recover route journal") ||
        expect((entry = wvm_control_plane_find_route_transaction(
                    &recovered_plane, transaction.operation_id)) != NULL &&
                   entry->state == WVM_ROUTE_TRANSACTION_RETIRED,
               "recover latest exact route state")) {
        wvm_control_plane_close(&recovered_plane);
        unlink(journal_path);
        return 1;
    }

    wvm_control_plane_close(&recovered_plane);
    unlink(journal_path);
    puts("control-route-journal tests: PASS");
    return 0;
}
