#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>

#include "executor_bridge.h"
#include "memory_service_v1.h"
#include "v1_ingress.h"
#include "../common_include/wavevm_executor_abi.h"
#include "../common_include/wavevm_route_delivery.h"
#include "../common_include/wavevm_route_runtime.h"
#include "../common_include/wavevm_runtime_dispatch.h"
#include "../common_include/wavevm_runtime_gate.h"
#include "../master_core/logic_core.h"

int wavevm_master_main(int argc, char **argv);
int wavevm_slave_main(int argc, char **argv);

struct role_launch {
    int argc;
    char **argv;
};

struct v1_memory_network_context {
    int sidecar_fd;
    struct sockaddr_in sidecar_address;
};

static void export_hex_env(const char *name, const uint8_t *bytes,
                           size_t byte_count)
{
    char *text;
    size_t i;

    text = calloc(byte_count * 2U + 1U, 1);
    if (!text) {
        fprintf(stderr, "[node-runtime] cannot export %s\n", name);
        return;
    }
    for (i = 0; i < byte_count; i++) {
        snprintf(text + i * 2U, 3, "%02x", bytes[i]);
    }
    if (setenv(name, text, 1) != 0) {
        fprintf(stderr, "[node-runtime] cannot set %s: %s\n", name,
                strerror(errno));
    }
    free(text);
}

static void export_runtime_identity(
    const struct wvm_node_runtime_manifest *manifest,
    uint64_t node_instance_id, int physical_node_id)
{
    char value[64];

    snprintf(value, sizeof(value), "%u", manifest->vm_id);
    setenv("WVM_VM_ID", value, 1);
    snprintf(value, sizeof(value), "%" PRIu64, manifest->vm_incarnation);
    setenv("WVM_VM_INCARNATION", value, 1);
    snprintf(value, sizeof(value), "%" PRIu64, manifest->manifest_generation);
    setenv("WVM_MANIFEST_GENERATION", value, 1);
    snprintf(value, sizeof(value), "%" PRIu64, node_instance_id);
    setenv("WVM_RUNTIME_LOCAL_INSTANCE_ID", value, 1);
    snprintf(value, sizeof(value), "%d", physical_node_id);
    setenv("WVM_RUNTIME_PHYSICAL_NODE_ID", value, 1);
    setenv("WVM_RUNTIME_NAMESPACE", manifest->local_names.namespace_name, 1);
    export_hex_env("WVM_CANDIDATE_MANIFEST_DIGEST",
                   manifest->candidate_manifest_digest,
                   sizeof(manifest->candidate_manifest_digest));
    if (manifest->has_activation_fence) {
        export_hex_env("WVM_ACTIVATION_FENCE", manifest->activation_fence,
                       sizeof(manifest->activation_fence));
    }
}

static int derive_runtime_profile_digest(
    const struct wvm_node_runtime_manifest *manifest,
    uint8_t digest[WVM_SHA256_DIGEST_BYTES])
{
    char error[256] = {0};

    if (wvm_runtime_manifest_profile_digest(manifest, digest, error,
                                            sizeof(error)) != 0) {
        fprintf(stderr, "[node-runtime] cannot derive profile digest: %s\n",
                error[0] ? error : "invalid capability profile");
        return -1;
    }
    return 0;
}

static void *run_master_role(void *opaque)
{
    struct role_launch *launch = opaque;
    int result = wavevm_master_main(launch->argc, launch->argv);

    return (void *)(intptr_t)result;
}

static int parse_u64(const char *text, uint64_t *value)
{
    char *end = NULL;
    unsigned long long parsed;

    if (!text || !*text || !value) {
        return -1;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed == 0) {
        return -1;
    }
    *value = (uint64_t)parsed;
    return 0;
}

static int parse_port_argument(char **argv, int index, uint16_t *port)
{
    long parsed;
    char *end = NULL;

    if (!argv || !argv[index] || !port) {
        return -1;
    }
    errno = 0;
    parsed = strtol(argv[index], &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed <= 0 ||
        parsed > UINT16_MAX) {
        return -1;
    }
    *port = (uint16_t)parsed;
    return 0;
}

static int make_role_argv(char **argv, int first, int last, char **out,
                          int capacity)
{
    int count = 0;
    int i;

    if (!argv || !out || capacity < 2 || first > last) {
        return -1;
    }
    out[count++] = argv[0];
    for (i = first; i < last; i++) {
        if (count + 1 >= capacity) {
            return -1;
        }
        out[count++] = argv[i];
    }
    out[count] = NULL;
    return count;
}

static int route_key_equal(const struct wvm_route_snapshot_key *left,
                           const struct wvm_route_snapshot_key *right)
{
    return left && right &&
           left->scope_key.vm_id == right->scope_key.vm_id &&
           left->scope_key.vm_incarnation == right->scope_key.vm_incarnation &&
           left->scope_key.route_scope_id == right->scope_key.route_scope_id &&
           left->topology_revision == right->topology_revision &&
           left->route_generation == right->route_generation &&
           memcmp(left->snapshot_digest, right->snapshot_digest,
                  WVM_SHA256_DIGEST_BYTES) == 0;
}

static int read_authoritative_local_page(
    void *opaque, uint64_t gpa, uint8_t data[WVM_V1_MEMORY_PAGE_BYTES],
    uint64_t *version_out, char *error, size_t error_len)
{
    int result;

    (void)opaque;
    if (!data || !version_out) {
        if (error && error_len != 0) {
            snprintf(error, error_len, "local V1 directory read is invalid");
        }
        return -EINVAL;
    }
    /*
     * This is a typed read-only adapter over the existing authoritative
     * directory page table. The page data points at the live WVM_SHM_FILE
     * mapping used by the local QEMU frontend; V1 ingress never reinterprets
     * a V1 payload as a legacy packet or invokes legacy network routing.
     */
    result = wvm_handle_local_fault_fastpath(gpa, data, version_out);
    if (result != 0 && error && error_len != 0) {
        snprintf(error, error_len,
                 "local V1 directory cannot read GPA %#" PRIx64, gpa);
    }
    return result;
}

static int commit_authoritative_local_page(
    void *opaque, uint64_t gpa, uint64_t base_version, uint16_t offset,
    const uint8_t *data, size_t data_bytes, uint64_t *result_version,
    char *error, size_t error_len)
{
    int result;

    (void)opaque;
    if (!data || !result_version) {
        if (error && error_len != 0) {
            snprintf(error, error_len,
                     "local V1 directory commit is invalid");
        }
        return -EINVAL;
    }
    result = wvm_handle_local_commit_v1(
        gpa, base_version, offset, data, data_bytes, result_version);
    if (result != 0 && error && error_len != 0) {
        snprintf(error, error_len,
                 "local V1 directory cannot commit GPA %#" PRIx64, gpa);
    }
    return result;
}

static int publish_authoritative_local_page(
    void *opaque, uint64_t gpa, uint64_t result_version, uint16_t offset,
    const uint8_t *data, size_t data_bytes, uint32_t writer_physical_node_id,
    char *error, size_t error_len)
{
    int result;

    (void)opaque;
    if (!data) {
        if (error && error_len != 0) {
            snprintf(error, error_len,
                     "local V1 directory publish is invalid");
        }
        return -EINVAL;
    }
    result = wvm_publish_local_commit_v1(
        gpa, result_version, offset, data, data_bytes,
        writer_physical_node_id);
    if (result != 0 && error && error_len != 0) {
        snprintf(error, error_len,
                 "local V1 directory cannot publish GPA %#" PRIx64, gpa);
    }
    return result;
}

static int send_v1_sidecar_frame(void *opaque, const uint8_t *frame,
                                 size_t frame_bytes, char *error,
                                 size_t error_len)
{
    struct v1_memory_network_context *context = opaque;
    ssize_t sent;

    if (!context || context->sidecar_fd < 0 || !frame || frame_bytes == 0) {
        if (error && error_len != 0) {
            snprintf(error, error_len, "local V1 sidecar transport is inactive");
        }
        return -ENOTCONN;
    }
    sent = sendto(context->sidecar_fd, frame, frame_bytes, MSG_DONTWAIT,
                  (const struct sockaddr *)&context->sidecar_address,
                  sizeof(context->sidecar_address));
    if (sent == (ssize_t)frame_bytes) {
        return 0;
    }
    if (error && error_len != 0) {
        snprintf(error, error_len, "local V1 sidecar send failed: %s",
                 sent < 0 ? strerror(errno) : "short datagram");
    }
    if (sent < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK ? -EAGAIN : -errno;
    }
    return -EIO;
}

static int send_v1_memory_envelope(void *opaque,
                                   const struct wvm_envelope_v1 *envelope,
                                   char *error, size_t error_len)
{
    return wvm_envelope_v1_emit_network_frames(
        envelope, send_v1_sidecar_frame, opaque, error, error_len);
}

static int complete_v1_memory_fault(
    void *opaque, const uint8_t operation_id[WVM_IDENTITY_ID_BYTES],
    uint64_t gpa, uint64_t version, uint16_t status,
    uint32_t directory_physical_node_id, uint64_t directory_node_instance_id,
    const uint8_t *data, size_t data_bytes, char *error, size_t error_len)
{
    (void)opaque;
    return wvm_v1_memory_service_global_complete(
        operation_id, gpa, version, status, directory_physical_node_id,
        directory_node_instance_id, data, data_bytes, error, error_len);
}

static int complete_v1_memory_commit(
    void *opaque, const uint8_t operation_id[WVM_IDENTITY_ID_BYTES],
    uint64_t gpa, uint16_t status, uint64_t result_version,
    uint32_t directory_physical_node_id, uint64_t directory_node_instance_id,
    char *error, size_t error_len)
{
    (void)opaque;
    return wvm_v1_memory_service_global_complete_commit(
        operation_id, gpa, status, result_version,
        directory_physical_node_id, directory_node_instance_id, error,
        error_len);
}

static int init_v1_memory_network_context(
    struct v1_memory_network_context *context,
    const struct wvm_runtime_dispatch_projection *dispatch, char *error,
    size_t error_len)
{
    const struct wvm_endpoint *endpoint;

    if (!context || !dispatch) {
        if (error && error_len != 0) {
            snprintf(error, error_len, "local V1 sidecar context is invalid");
        }
        return -EINVAL;
    }
    endpoint = &dispatch->local_sidecar_endpoint;
    if (endpoint->data_transport != WVM_DATA_TRANSPORT_UDP ||
        endpoint->data_address_bytes != 4 || endpoint->data_port == 0) {
        if (error && error_len != 0) {
            snprintf(error, error_len,
                     "local V1 sidecar endpoint is not IPv4/UDP");
        }
        return -EPROTONOSUPPORT;
    }
    memset(context, 0, sizeof(*context));
    context->sidecar_fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (context->sidecar_fd < 0) {
        if (error && error_len != 0) {
            snprintf(error, error_len, "cannot create V1 sidecar socket: %s",
                     strerror(errno));
        }
        return -errno;
    }
    context->sidecar_address.sin_family = AF_INET;
    memcpy(&context->sidecar_address.sin_addr.s_addr, endpoint->data_address,
           sizeof(context->sidecar_address.sin_addr.s_addr));
    context->sidecar_address.sin_port = htons(endpoint->data_port);
    return 0;
}

static void destroy_v1_memory_network_context(
    struct v1_memory_network_context *context)
{
    if (!context) {
        return;
    }
    if (context->sidecar_fd >= 0) {
        close(context->sidecar_fd);
    }
    memset(context, 0, sizeof(*context));
    context->sidecar_fd = -1;
}

static int runtime_dispatch_matches_manifest(
    const struct wvm_runtime_dispatch_projection *dispatch,
    const struct wvm_node_runtime_manifest *manifest,
    uint64_t node_instance_id, uint32_t physical_node_id, char *error,
    size_t error_len)
{
    if (!dispatch || !manifest ||
        wvm_runtime_dispatch_projection_validate(dispatch, error,
                                                 error_len) != 0 ||
        memcmp(dispatch->candidate_manifest_digest,
               manifest->candidate_manifest_digest,
               sizeof(dispatch->candidate_manifest_digest)) != 0 ||
        dispatch->vm_id != manifest->vm_id ||
        dispatch->vm_incarnation != manifest->vm_incarnation ||
        dispatch->manifest_generation != manifest->manifest_generation ||
        dispatch->physical_node_id != physical_node_id ||
        dispatch->expected_node_instance_id != node_instance_id ||
        memcmp(dispatch->activation_fence, manifest->activation_fence,
               sizeof(dispatch->activation_fence)) != 0 ||
        !route_key_equal(&dispatch->required_route_snapshot_key,
                         &manifest->required_route_snapshot_key)) {
        if (error && error_len != 0) {
            snprintf(error, error_len,
                     "runtime dispatch does not match admitted manifest");
        }
        return -1;
    }
    return 0;
}

static int prepare_runtime_gate(const char *manifest_path,
                                uint64_t node_instance_id,
                                int physical_node_id,
                                struct wvm_runtime_manifest_storage *storage,
                                struct wvm_runtime_gate *gate,
                                char *route_snapshot_path,
                                size_t route_snapshot_path_capacity,
                                char *dispatch_path,
                                size_t dispatch_path_capacity,
                                struct wvm_runtime_dispatch_storage
                                    *dispatch_storage,
                                struct wvm_route_runtime *route_runtime,
                                uint64_t *completion_timeout_ms)
{
    struct wvm_route_snapshot_file_storage route_storage;
    char error[256] = {0};

    if (!completion_timeout_ms) {
        return -1;
    }
    *completion_timeout_ms = 0;
    wvm_route_snapshot_file_storage_init(&route_storage);
    wvm_runtime_manifest_storage_init(storage);
    wvm_runtime_dispatch_storage_init(dispatch_storage);
    wvm_route_runtime_init(route_runtime);
    if (wvm_runtime_manifest_load_file(manifest_path, storage, error,
                                       sizeof(error)) != 0 ||
        wvm_runtime_gate_prepare(gate, &storage->manifest,
                                 (uint32_t)physical_node_id, node_instance_id,
                                 error, sizeof(error)) != 0 ||
        wvm_runtime_gate_activate(gate, storage->manifest.activation_fence,
                                  error, sizeof(error)) != 0) {
        fprintf(stderr, "[node-runtime] manifest gate rejected startup: %s\n",
                error[0] ? error : "invalid runtime manifest");
        wvm_runtime_manifest_storage_free(storage);
        wvm_route_snapshot_file_storage_free(&route_storage);
        wvm_runtime_dispatch_storage_free(dispatch_storage);
        wvm_route_runtime_destroy(route_runtime);
        return -1;
    }
    if (wvm_route_snapshot_path_from_manifest(
            manifest_path, route_snapshot_path, route_snapshot_path_capacity,
            error, sizeof(error)) != 0 ||
        wvm_route_snapshot_file_load(route_snapshot_path, &route_storage, error,
                                     sizeof(error)) != 0 ||
        wvm_route_snapshot_file_matches(
            &route_storage,
            &storage->manifest.required_route_snapshot_key, error,
            sizeof(error)) != 0) {
        fprintf(stderr,
                "[node-runtime] admitted route snapshot rejected: %s\n",
                error[0] ? error : "route snapshot identity mismatch");
        wvm_runtime_manifest_storage_free(storage);
        wvm_route_snapshot_file_storage_free(&route_storage);
        wvm_runtime_dispatch_storage_free(dispatch_storage);
        wvm_route_runtime_destroy(route_runtime);
        return -1;
    }
    if (wvm_route_runtime_prepare(route_runtime, &route_storage.snapshot,
                                  error, sizeof(error)) != 0 ||
        wvm_route_runtime_activate(
            route_runtime, &storage->manifest.required_route_snapshot_key,
            error, sizeof(error)) != 0) {
        fprintf(stderr,
                "[node-runtime] immutable route runtime rejected startup: %s\n",
                error[0] ? error : "route snapshot activation failed");
        wvm_runtime_manifest_storage_free(storage);
        wvm_route_snapshot_file_storage_free(&route_storage);
        wvm_runtime_dispatch_storage_free(dispatch_storage);
        wvm_route_runtime_destroy(route_runtime);
        return -1;
    }
    *completion_timeout_ms =
        route_storage.snapshot.operation_retention_horizon_ms;
    if (*completion_timeout_ms == 0) {
        fprintf(stderr,
                "[node-runtime] route snapshot has no operation retention "
                "horizon\n");
        wvm_runtime_manifest_storage_free(storage);
        wvm_route_snapshot_file_storage_free(&route_storage);
        wvm_runtime_dispatch_storage_free(dispatch_storage);
        wvm_route_runtime_destroy(route_runtime);
        return -1;
    }
    if (wvm_runtime_dispatch_path_from_manifest(
            manifest_path, dispatch_path, dispatch_path_capacity, error,
            sizeof(error)) != 0 ||
        wvm_runtime_dispatch_file_load(dispatch_path, dispatch_storage, error,
                                       sizeof(error)) != 0 ||
        runtime_dispatch_matches_manifest(
            &dispatch_storage->projection, &storage->manifest,
            node_instance_id, (uint32_t)physical_node_id, error,
            sizeof(error)) != 0) {
        fprintf(stderr,
                "[node-runtime] admitted runtime dispatch rejected: %s\n",
                error[0] ? error : "runtime dispatch identity mismatch");
        wvm_runtime_manifest_storage_free(storage);
        wvm_route_snapshot_file_storage_free(&route_storage);
        wvm_runtime_dispatch_storage_free(dispatch_storage);
        wvm_route_runtime_destroy(route_runtime);
        return -1;
    }
    wvm_route_snapshot_file_storage_free(&route_storage);
    return 0;
}

static int parse_required_options(int argc, char **argv, const char **manifest,
                                  uint64_t *node_instance_id)
{
    int i;
    int have_manifest = 0;
    int have_instance = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--manifest") == 0 && i + 1 < argc) {
            *manifest = argv[++i];
            have_manifest = 1;
        } else if (strcmp(argv[i], "--node-instance") == 0 &&
                   i + 1 < argc &&
                   parse_u64(argv[++i], node_instance_id) == 0) {
            have_instance = 1;
        }
    }
    return have_manifest && have_instance ? 0 : -1;
}

static int validate_role_vm_arguments(
    const struct wvm_node_runtime_manifest *manifest, int master_argc,
    char **master_argv, int executor_argc, char **executor_argv)
{
    int master_vm_id;
    int executor_vm_id;

    if (!manifest || master_argc < 9 || executor_argc < 7) {
        fprintf(stderr,
                "[node-runtime] gated launch requires VM IDs in both role "
                "argument groups\n");
        return -1;
    }
    /*
     * The unified process still executes the legacy header data plane.  A
     * V1_U32 manifest must not be silently narrowed into its 8-bit VM field.
     */
    if (manifest->vm_id > UINT8_MAX) {
        fprintf(stderr,
                "[node-runtime] admitted vm_id=%u requires the V1_U32 "
                "data-plane envelope; legacy master/executor roles refuse "
                "to truncate it\n",
                manifest->vm_id);
        return -1;
    }
    master_vm_id = atoi(master_argv[8]);
    executor_vm_id = atoi(executor_argv[6]);
    if (master_vm_id <= 0 || executor_vm_id <= 0 ||
        (uint32_t)master_vm_id != manifest->vm_id ||
        (uint32_t)executor_vm_id != manifest->vm_id) {
        fprintf(stderr,
                "[node-runtime] role VM IDs do not match admitted vm_id=%u\n",
                manifest->vm_id);
        return -1;
    }
    return 0;
}

/*
 * One process image owns both logical roles. The master role remains the
 * node-runtime semantic coordinator; the slave role remains its local
 * executor manager. They are started on separate threads and retain their
 * existing asynchronous queues instead of forwarding through a new global
 * lock.
 *
 * Invocation:
 *   wavevm_node_runtime --manifest FILE --node-instance N
 *     --master <existing master args...>
 *     --executor <existing slave args...>
 */
int main(int argc, char **argv)
{
    const char *manifest_path = NULL;
    uint64_t node_instance_id = 0;
    int master_marker = -1;
    int executor_marker = -1;
    char *master_argv[32];
    char *executor_argv[32];
    int master_argc;
    int executor_argc;
    int physical_node_id;
    pthread_t master_thread;
    struct role_launch master_launch;
    struct wvm_runtime_manifest_storage storage;
    struct wvm_runtime_gate gate;
    char route_snapshot_path[WVM_ROUTE_DELIVERY_PATH_MAX];
    char dispatch_path[WVM_RUNTIME_DISPATCH_PATH_MAX];
    struct wvm_runtime_dispatch_storage dispatch_storage;
    struct wvm_route_runtime route_runtime;
    struct wvm_v1_memory_service memory_service;
    struct v1_memory_network_context memory_network;
    uint8_t capability_profile_digest[WVM_SHA256_DIGEST_BYTES];
    uint64_t completion_timeout_ms = 0;
    uint64_t runtime_connection_id = 0;
    void *master_result = NULL;
    int i;
    char instance_text[32];

    if (parse_required_options(argc, argv, &manifest_path,
                               &node_instance_id) != 0) {
        fprintf(stderr,
                "Usage: %s --manifest FILE --node-instance N "
                "--master <args...> --executor <args...>\n",
                argv[0]);
        return 2;
    }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--master") == 0) {
            master_marker = i + 1;
        } else if (strcmp(argv[i], "--executor") == 0) {
            executor_marker = i + 1;
            break;
        }
    }
    if (master_marker < 0 || executor_marker <= master_marker ||
        executor_marker >= argc || argc - executor_marker >= 32 ||
        master_marker >= argc || executor_marker - master_marker >= 32) {
        fprintf(stderr, "[node-runtime] both role argument groups are required\n");
        return 2;
    }
    master_argc = make_role_argv(argv, master_marker, executor_marker,
                                 master_argv,
                                 (int)(sizeof(master_argv) /
                                       sizeof(master_argv[0])));
    executor_argc = make_role_argv(argv, executor_marker, argc, executor_argv,
                                   (int)(sizeof(executor_argv) /
                                         sizeof(executor_argv[0])));
    if (master_argc < 0 || executor_argc < 0 || master_argc < 5 ||
        executor_argc < 5) {
        fprintf(stderr, "[node-runtime] role arguments are incomplete\n");
        return 2;
    }

    /*
     * The existing master ABI keeps the physical node at argument 4. The
     * unified entry validates that identity before either role opens a socket.
     */
    memset(&memory_service, 0, sizeof(memory_service));
    memset(&memory_network, 0, sizeof(memory_network));
    memory_network.sidecar_fd = -1;
    physical_node_id = atoi(master_argv[4]);
    if (physical_node_id <= 0 ||
        prepare_runtime_gate(manifest_path, node_instance_id, physical_node_id,
                             &storage, &gate, route_snapshot_path,
                             sizeof(route_snapshot_path), dispatch_path,
                             sizeof(dispatch_path), &dispatch_storage,
                             &route_runtime, &completion_timeout_ms) != 0) {
        return 1;
    }
    if (validate_role_vm_arguments(&storage.manifest, master_argc, master_argv,
                                   executor_argc, executor_argv) != 0) {
        wvm_runtime_manifest_storage_free(&storage);
        wvm_runtime_dispatch_storage_free(&dispatch_storage);
        wvm_route_runtime_destroy(&route_runtime);
        return 1;
    }
    snprintf(instance_text, sizeof(instance_text), "%llu",
             (unsigned long long)node_instance_id);
    setenv("WVM_RUNTIME_MANIFEST_PATH", manifest_path, 1);
    setenv("WVM_RUNTIME_ROUTE_SNAPSHOT_PATH", route_snapshot_path, 1);
    setenv("WVM_RUNTIME_DISPATCH_PATH", dispatch_path, 1);
    setenv("WVM_NODE_INSTANCE_ID", instance_text, 1);
    setenv("WVM_RUNTIME_GATE_ACTIVE", "1", 1);
    export_runtime_identity(&storage.manifest, node_instance_id,
                            physical_node_id);
    {
        char value[64];

        snprintf(value, sizeof(value), "%" PRIu64,
                 storage.manifest.required_route_snapshot_key.scope_key
                     .route_scope_id);
        setenv("WVM_ROUTE_SCOPE_ID", value, 1);
        snprintf(value, sizeof(value), "%" PRIu64,
                 storage.manifest.required_route_snapshot_key.topology_revision);
        setenv("WVM_TOPOLOGY_REVISION", value, 1);
        snprintf(value, sizeof(value), "%" PRIu64,
                 storage.manifest.required_route_snapshot_key.route_generation);
        setenv("WVM_ROUTE_GENERATION", value, 1);
        export_hex_env("WVM_ROUTE_SNAPSHOT_DIGEST",
                       storage.manifest.required_route_snapshot_key
                           .snapshot_digest,
                       sizeof(storage.manifest.required_route_snapshot_key
                                  .snapshot_digest));
        snprintf(value, sizeof(value), "%u",
                 (unsigned)dispatch_storage.projection.local_primary
                     .destination_kind);
        setenv("WVM_RUNTIME_LOCAL_PRIMARY_DESTINATION_KIND", value, 1);
        snprintf(value, sizeof(value), "%" PRIu64,
                 dispatch_storage.projection.local_primary.destination_scope);
        setenv("WVM_RUNTIME_LOCAL_PRIMARY_DESTINATION_SCOPE", value, 1);
        snprintf(value, sizeof(value), "%u",
                 (unsigned)dispatch_storage.projection.local_primary
                     .destination_vnode);
        setenv("WVM_RUNTIME_LOCAL_PRIMARY_DESTINATION_VNODE", value, 1);
    }
    if (derive_runtime_profile_digest(&storage.manifest,
                                      capability_profile_digest) != 0) {
        wvm_runtime_manifest_storage_free(&storage);
        wvm_runtime_dispatch_storage_free(&dispatch_storage);
        wvm_route_runtime_destroy(&route_runtime);
        return 1;
    }
    export_hex_env("WVM_CAPABILITY_PROFILE_DIGEST", capability_profile_digest,
                   sizeof(capability_profile_digest));
    {
        struct wvm_runtime_registration registration;
        char error[256] = {0};

        memset(&registration, 0, sizeof(registration));
        registration.connection_role = WVM_MANIFEST_ROLE_NODE_RUNTIME;
        registration.vm_id = storage.manifest.vm_id;
        registration.vm_incarnation = storage.manifest.vm_incarnation;
        registration.manifest_generation = storage.manifest.manifest_generation;
        memcpy(registration.candidate_manifest_digest,
               storage.manifest.candidate_manifest_digest,
               sizeof(registration.candidate_manifest_digest));
        registration.local_runtime_instance_id = node_instance_id;
        registration.caller_process_instance_id = (uint64_t)getpid();
        memcpy(registration.capability_profile_digest, capability_profile_digest,
               sizeof(registration.capability_profile_digest));
        snprintf(registration.requested_endpoint_name,
                 sizeof(registration.requested_endpoint_name), "%s",
                 storage.manifest.local_names.namespace_name);
        if (wvm_runtime_gate_register(&gate, &registration,
                                      &runtime_connection_id, error,
                                      sizeof(error)) != 0) {
            fprintf(stderr,
                    "[node-runtime] cannot register admitted local caller: %s\n",
                    error[0] ? error : "runtime gate rejected registration");
            wvm_runtime_manifest_storage_free(&storage);
            wvm_runtime_dispatch_storage_free(&dispatch_storage);
            wvm_route_runtime_destroy(&route_runtime);
            return 1;
        }
    }

    fprintf(stderr,
            "[node-runtime] admitted vm=%u incarnation=%" PRIu64
            " generation=%" PRIu64 " physical_node=%d\n",
            storage.manifest.vm_id, storage.manifest.vm_incarnation,
            storage.manifest.manifest_generation, physical_node_id);

    {
        uint16_t executor_service_port;
        uint16_t node_runtime_port;
        char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
        struct wvm_executor_bridge_config bridge_config;
        struct wvm_v1_memory_service_config memory_config;
        struct wvm_v1_ingress_config ingress_config;
        char error[256] = {0};

        if (parse_port_argument(executor_argv, 1, &executor_service_port) != 0 ||
            parse_port_argument(master_argv, 2, &node_runtime_port) != 0 ||
            snprintf(socket_path, sizeof(socket_path), "/tmp/%s-executor.sock",
                     storage.manifest.local_names.namespace_name) >=
                (int)sizeof(socket_path)) {
            fprintf(stderr, "[node-runtime] cannot derive executor ABI endpoint\n");
            wvm_runtime_manifest_storage_free(&storage);
            wvm_runtime_dispatch_storage_free(&dispatch_storage);
            wvm_route_runtime_destroy(&route_runtime);
            return 1;
        }
        setenv("WVM_LOCAL_EXECUTOR_SOCKET", socket_path, 1);
        snprintf(instance_text, sizeof(instance_text), "%u",
                 (unsigned)executor_service_port);
        setenv("WVM_EXECUTOR_SERVICE_PORT", instance_text, 1);
        snprintf(instance_text, sizeof(instance_text), "%u",
                 (unsigned)node_runtime_port);
        setenv("WVM_RUNTIME_LOCAL_PORT", instance_text, 1);
        memset(&bridge_config, 0, sizeof(bridge_config));
        bridge_config.manifest = &storage.manifest;
        bridge_config.runtime_gate = &gate;
        bridge_config.local_runtime_instance_id = node_instance_id;
        bridge_config.runtime_connection_id = runtime_connection_id;
        bridge_config.socket_path = socket_path;
        bridge_config.executor_service_port = executor_service_port;
        bridge_config.node_runtime_port = node_runtime_port;
        if (wvm_executor_bridge_start(&bridge_config, NULL) != 0) {
            fprintf(stderr, "[node-runtime] cannot start executor ABI bridge\n");
            wvm_runtime_manifest_storage_free(&storage);
            wvm_runtime_dispatch_storage_free(&dispatch_storage);
            wvm_route_runtime_destroy(&route_runtime);
            return 1;
        }
        if (init_v1_memory_network_context(
                &memory_network, &dispatch_storage.projection, error,
                sizeof(error)) != 0) {
            fprintf(stderr,
                    "[node-runtime] cannot initialize local V1 sidecar "
                    "transport: %s\n",
                    error[0] ? error : "invalid sidecar endpoint");
            wvm_runtime_manifest_storage_free(&storage);
            wvm_runtime_dispatch_storage_free(&dispatch_storage);
            wvm_route_runtime_destroy(&route_runtime);
            return 1;
        }
        memset(&memory_config, 0, sizeof(memory_config));
        memory_config.dispatch = &dispatch_storage.projection;
        memory_config.route_runtime = &route_runtime;
        memory_config.local_physical_node_id = (uint32_t)physical_node_id;
        memory_config.local_node_instance_id = node_instance_id;
        memory_config.local_runtime_instance_id = node_instance_id;
        memory_config.completion_timeout_ms = completion_timeout_ms;
        memory_config.read_page = read_authoritative_local_page;
        memory_config.commit_page = commit_authoritative_local_page;
        memory_config.publish_commit = publish_authoritative_local_page;
        memory_config.complete_commit = complete_v1_memory_commit;
        memory_config.complete_fault = complete_v1_memory_fault;
        memory_config.send_envelope = send_v1_memory_envelope;
        memory_config.opaque = &memory_network;
        if (wvm_v1_memory_service_init(&memory_service, &memory_config, error,
                                       sizeof(error)) != 0 ||
            wvm_v1_memory_service_global_install(&memory_service, error,
                                                 sizeof(error)) != 0) {
            fprintf(stderr,
                    "[node-runtime] cannot initialize V1 memory service: %s\n",
                    error[0] ? error : "invalid V1 memory state");
            wvm_v1_memory_service_destroy(&memory_service);
            destroy_v1_memory_network_context(&memory_network);
            wvm_runtime_manifest_storage_free(&storage);
            wvm_runtime_dispatch_storage_free(&dispatch_storage);
            wvm_route_runtime_destroy(&route_runtime);
            return 1;
        }
        memset(&ingress_config, 0, sizeof(ingress_config));
        ingress_config.manifest = &storage.manifest;
        ingress_config.runtime_gate = &gate;
        ingress_config.runtime_connection_id = runtime_connection_id;
        ingress_config.dispatch = wvm_v1_memory_service_dispatch;
        ingress_config.dispatch_opaque = &memory_service;
        if (wvm_v1_ingress_global_init(&ingress_config, error,
                                       sizeof(error)) != 0) {
            fprintf(stderr, "[node-runtime] cannot initialize V1 ingress: %s\n",
                    error[0] ? error : "invalid admitted ingress state");
            wvm_v1_memory_service_global_uninstall(&memory_service);
            wvm_v1_memory_service_destroy(&memory_service);
            destroy_v1_memory_network_context(&memory_network);
            wvm_runtime_manifest_storage_free(&storage);
            wvm_runtime_dispatch_storage_free(&dispatch_storage);
            wvm_route_runtime_destroy(&route_runtime);
            return 1;
        }
    }

    master_launch.argc = master_argc;
    master_launch.argv = master_argv;
    if (pthread_create(&master_thread, NULL, run_master_role,
                       &master_launch) != 0) {
        perror("[node-runtime] cannot start node-runtime coordinator role");
        wvm_v1_memory_service_global_uninstall(&memory_service);
        wvm_v1_memory_service_destroy(&memory_service);
        destroy_v1_memory_network_context(&memory_network);
        wvm_runtime_manifest_storage_free(&storage);
        wvm_runtime_dispatch_storage_free(&dispatch_storage);
        wvm_route_runtime_destroy(&route_runtime);
        return 1;
    }

    /*
     * The executor role runs in this thread because its existing main owns
     * the KVM/TCG worker lifecycle. The master thread remains independent and
     * continues to service QEMU/fabric traffic concurrently.
     */
    (void)wavevm_slave_main(executor_argc, executor_argv);
    pthread_join(master_thread, &master_result);
    wvm_v1_memory_service_global_uninstall(&memory_service);
    wvm_v1_memory_service_destroy(&memory_service);
    destroy_v1_memory_network_context(&memory_network);
    wvm_runtime_manifest_storage_free(&storage);
    wvm_runtime_dispatch_storage_free(&dispatch_storage);
    wvm_route_runtime_destroy(&route_runtime);
    return (int)(intptr_t)master_result;
}
