#include <stdio.h>
#include <string.h>

#include "wavevm_manifest.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "manifest-primitives test: %s\n", message);
        return -1;
    }
    return 0;
}

int main(void)
{
    struct wvm_vm_route_scope_key scope = {256, 4, 9};
    struct wvm_vm_route_scope_key decoded_scope;
    struct wvm_route_snapshot_key snapshot;
    struct wvm_route_snapshot_key decoded_snapshot;
    struct wvm_local_name_identity identity;
    struct wvm_local_name_namespace namespace_value;
    struct wvm_local_name_namespace decoded_namespace;
    struct wvm_vcpu_assignment vcpu;
    struct wvm_vcpu_assignment decoded_vcpu;
    struct wvm_memory_chunk_assignment memory;
    struct wvm_memory_chunk_assignment decoded_memory;
    uint8_t bytes[512];
    size_t encoded_bytes;
    char error[256] = {0};

    if (expect(wvm_vm_route_scope_key_encode(&scope, bytes, sizeof(bytes),
                                             &encoded_bytes, error,
                                             sizeof(error)) == 0,
               "encode scope") ||
        expect(wvm_vm_route_scope_key_decode(bytes, encoded_bytes,
                                             &decoded_scope, error,
                                             sizeof(error)) == 0,
               "decode scope") ||
        expect(memcmp(&scope, &decoded_scope, sizeof(scope)) == 0,
               "scope round trip")) {
        return 1;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.scope_key = scope;
    snapshot.topology_revision = 7;
    snapshot.route_generation = 2;
    memset(snapshot.snapshot_digest, 0x1a, sizeof(snapshot.snapshot_digest));
    if (expect(wvm_route_snapshot_key_encode(&snapshot, bytes, sizeof(bytes),
                                             &encoded_bytes, error,
                                             sizeof(error)) == 0,
               "encode snapshot key") ||
        expect(wvm_route_snapshot_key_decode(bytes, encoded_bytes,
                                             &decoded_snapshot, error,
                                             sizeof(error)) == 0,
               "decode snapshot key") ||
        expect(memcmp(&snapshot, &decoded_snapshot, sizeof(snapshot)) == 0,
               "snapshot key round trip")) {
        return 1;
    }

    memset(&identity, 0, sizeof(identity));
    identity.vm_id = 256;
    identity.vm_incarnation = 4;
    identity.manifest_generation = 1;
    identity.physical_node_id = 17;
    memset(identity.manifest_id, 0x2b, sizeof(identity.manifest_id));
    memset(identity.admission_tx_id, 0xb2, sizeof(identity.admission_tx_id));
    if (expect(wvm_local_name_namespace_derive(&identity, &namespace_value,
                                                error, sizeof(error)) == 0,
               "derive namespace") ||
        expect(wvm_local_name_namespace_encode(&namespace_value, bytes,
                                               sizeof(bytes), &encoded_bytes,
                                               error, sizeof(error)) == 0,
               "encode namespace") ||
        expect(wvm_local_name_namespace_decode(bytes, encoded_bytes,
                                               &decoded_namespace, error,
                                               sizeof(error)) == 0,
               "decode namespace") ||
        expect(memcmp(&namespace_value, &decoded_namespace,
                      sizeof(namespace_value)) == 0,
               "namespace round trip")) {
        return 1;
    }

    memset(&vcpu, 0, sizeof(vcpu));
    vcpu.guest_vcpu_index = 3;
    vcpu.executor_physical_node_id = 99;
    vcpu.backend = WVM_MANIFEST_BACKEND_TCG;
    vcpu.executor_class = 1;
    vcpu.executor_slot = 4;
    memset(vcpu.reservation_id, 0x3c, sizeof(vcpu.reservation_id));
    if (expect(wvm_vcpu_assignment_encode(&vcpu, bytes, sizeof(bytes),
                                          &encoded_bytes, error,
                                          sizeof(error)) == 0,
               "encode vCPU assignment") ||
        expect(wvm_vcpu_assignment_decode(bytes, encoded_bytes, &decoded_vcpu,
                                          error, sizeof(error)) == 0,
               "decode vCPU assignment") ||
        expect(memcmp(&vcpu, &decoded_vcpu, sizeof(vcpu)) == 0,
               "vCPU assignment round trip")) {
        return 1;
    }

    memset(&memory, 0, sizeof(memory));
    memory.gpa_start = 2 * 1024 * 1024;
    memory.bytes = 2 * 1024 * 1024;
    memory.directory_physical_node_id = 17;
    memory.executor_physical_node_id = 99;
    memory.consistency_policy = 1;
    memset(memory.reservation_id, 0x4d, sizeof(memory.reservation_id));
    if (expect(wvm_memory_chunk_assignment_encode(&memory, bytes,
                                                  sizeof(bytes), &encoded_bytes,
                                                  error, sizeof(error)) == 0,
               "encode memory assignment") ||
        expect(wvm_memory_chunk_assignment_decode(bytes, encoded_bytes,
                                                  &decoded_memory, error,
                                                  sizeof(error)) == 0,
               "decode memory assignment") ||
        expect(memcmp(&memory, &decoded_memory, sizeof(memory)) == 0,
               "memory assignment round trip")) {
        return 1;
    }

    if (expect(wvm_vm_route_scope_key_decode(bytes, encoded_bytes,
                                             &decoded_scope, error,
                                             sizeof(error)) != 0,
               "reject wrong record decoder")) {
        return 1;
    }

    puts("manifest-primitives tests: PASS");
    return 0;
}
