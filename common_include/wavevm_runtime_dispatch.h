#ifndef WAVEVM_RUNTIME_DISPATCH_H
#define WAVEVM_RUNTIME_DISPATCH_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_cluster.h"
#include "wavevm_route_delivery.h"

#define WVM_RECORD_RUNTIME_DISPATCH_PROJECTION 0x1028U
#define WVM_RUNTIME_DISPATCH_MAX_BYTES (4U * 1024U * 1024U)
#define WVM_RUNTIME_DISPATCH_PATH_MAX 4096U

/*
 * This is a derived cache for the legacy logic-core adapter.  It is not a
 * second placement or routing authority: every field is bound to one admitted
 * manifest, one activated local runtime projection, and one route snapshot.
 */
struct wvm_runtime_cpu_dispatch {
    uint32_t guest_vcpu_index;
    uint32_t executor_vnode;
};

struct wvm_runtime_memory_dispatch {
    uint64_t gpa_start;
    uint64_t bytes;
    uint32_t directory_vnode;
    uint32_t executor_vnode;
    uint16_t consistency_policy;
};

struct wvm_runtime_cpu_dispatch_list {
    struct wvm_runtime_cpu_dispatch *entries;
    size_t count;
    size_t capacity;
};

struct wvm_runtime_memory_dispatch_list {
    struct wvm_runtime_memory_dispatch *entries;
    size_t count;
    size_t capacity;
};

struct wvm_runtime_dispatch_projection {
    uint8_t candidate_manifest_digest[WVM_SHA256_DIGEST_BYTES];
    uint32_t vm_id;
    uint64_t vm_incarnation;
    uint64_t manifest_generation;
    uint32_t physical_node_id;
    uint64_t expected_node_instance_id;
    uint8_t activation_fence[WVM_IDENTITY_ID_BYTES];
    struct wvm_route_snapshot_key required_route_snapshot_key;
    uint32_t local_primary_vnode;
    uint32_t route_vnode_count;
    struct wvm_endpoint local_sidecar_endpoint;
    struct wvm_runtime_cpu_dispatch_list cpu_dispatch;
    struct wvm_runtime_memory_dispatch_list memory_dispatch;
};

struct wvm_runtime_dispatch_storage {
    struct wvm_runtime_dispatch_projection projection;
    struct wvm_runtime_cpu_dispatch *cpu_entries;
    struct wvm_runtime_memory_dispatch *memory_entries;
};

void wvm_runtime_dispatch_storage_init(
    struct wvm_runtime_dispatch_storage *storage);
void wvm_runtime_dispatch_storage_free(
    struct wvm_runtime_dispatch_storage *storage);

/*
 * Compile the minimal runtime cache required by the current logic-core
 * adapter.  Cluster records supply physical-node to vnode/sidecar bindings;
 * the immutable candidate supplies placement; the route snapshot supplies the
 * active local sidecar endpoint.
 */
int wvm_runtime_dispatch_projection_build(
    const struct wvm_candidate_vm_manifest *candidate,
    const struct wvm_node_runtime_manifest *runtime_manifest,
    const struct wvm_cluster_record_set *records,
    const struct wvm_route_snapshot_record *route_snapshot,
    struct wvm_runtime_dispatch_projection *projection, char *error,
    size_t error_len);

int wvm_runtime_dispatch_projection_validate(
    const struct wvm_runtime_dispatch_projection *projection, char *error,
    size_t error_len);
int wvm_runtime_dispatch_projection_encode(
    const struct wvm_runtime_dispatch_projection *projection, uint8_t *bytes,
    size_t capacity, size_t *encoded_bytes, char *error, size_t error_len);
int wvm_runtime_dispatch_projection_decode(
    const uint8_t *bytes, size_t encoded_bytes,
    struct wvm_runtime_dispatch_projection *projection, char *error,
    size_t error_len);

int wvm_runtime_dispatch_path_from_manifest(
    const char *manifest_path, char *dispatch_path, size_t dispatch_path_capacity,
    char *error, size_t error_len);
int wvm_runtime_dispatch_file_publish(
    const char *path, const struct wvm_runtime_dispatch_projection *projection,
    char *error, size_t error_len);
int wvm_runtime_dispatch_file_load(
    const char *path, struct wvm_runtime_dispatch_storage *storage,
    char *error, size_t error_len);

#endif /* WAVEVM_RUNTIME_DISPATCH_H */
