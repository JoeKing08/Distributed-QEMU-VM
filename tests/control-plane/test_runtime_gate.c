#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "wavevm_runtime_gate.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "runtime-gate test: %s\n", message);
        return -1;
    }
    return 0;
}

int main(void)
{
    struct wvm_node_runtime_manifest manifest;
    struct wvm_runtime_gate gate;
    struct wvm_runtime_registration registration;
    struct wvm_runtime_operation operation;
    struct wvm_capability_ref capability;
    struct wvm_local_name_identity name_identity;
    struct wvm_runtime_manifest_storage loaded_storage;
    uint8_t profile_digest[WVM_SHA256_DIGEST_BYTES];
    uint64_t connection_id = 0;
    char manifest_path[] = "/tmp/wavevm-runtime-manifest.XXXXXX";
    char error[256] = {0};
    int manifest_fd;

    memset(&manifest, 0, sizeof(manifest));
    memset(manifest.candidate_manifest_digest, 0x11,
           sizeof(manifest.candidate_manifest_digest));
    manifest.vm_id = 256;
    manifest.vm_incarnation = 4;
    manifest.manifest_generation = 1;
    memset(manifest.admission_tx_id, 0x22, sizeof(manifest.admission_tx_id));
    memset(manifest.eligibility_fence_digest, 0x33,
           sizeof(manifest.eligibility_fence_digest));
    manifest.has_activation_fence = 1;
    memset(manifest.activation_fence, 0x44, sizeof(manifest.activation_fence));
    manifest.physical_node_id = 17;
    manifest.expected_node_instance_id = 101;
    manifest.local_role_bits =
        WVM_RUNTIME_ROLE_BIT(WVM_MANIFEST_ROLE_NODE_RUNTIME) |
        WVM_RUNTIME_ROLE_BIT(WVM_MANIFEST_ROLE_QEMU_FRONTEND) |
        WVM_RUNTIME_ROLE_BIT(WVM_MANIFEST_ROLE_EXECUTOR);
    manifest.required_route_snapshot_key.scope_key.vm_id = manifest.vm_id;
    manifest.required_route_snapshot_key.scope_key.vm_incarnation =
        manifest.vm_incarnation;
    manifest.required_route_snapshot_key.scope_key.route_scope_id = 9;
    manifest.required_route_snapshot_key.topology_revision = 2;
    manifest.required_route_snapshot_key.route_generation = 3;
    memset(manifest.required_route_snapshot_key.snapshot_digest, 0x55,
           sizeof(manifest.required_route_snapshot_key.snapshot_digest));
    memset(&name_identity, 0, sizeof(name_identity));
    name_identity.vm_id = manifest.vm_id;
    name_identity.vm_incarnation = manifest.vm_incarnation;
    name_identity.manifest_generation = manifest.manifest_generation;
    name_identity.physical_node_id = manifest.physical_node_id;
    memset(name_identity.manifest_id, 0x66, sizeof(name_identity.manifest_id));
    memcpy(name_identity.admission_tx_id, manifest.admission_tx_id,
           sizeof(name_identity.admission_tx_id));
    if (expect(wvm_local_name_namespace_derive(
                   &name_identity, &manifest.local_names, error,
                   sizeof(error)) == 0,
               "derive local namespace")) {
        return 1;
    }
    manifest.negotiated_profile.backend = WVM_MANIFEST_BACKEND_TCG;
    manifest.negotiated_profile.context_schema_version = 1;
    manifest.negotiated_profile.dirty_capture_engine = 1;
    manifest.negotiated_profile.read_fault_engine = 1;
    manifest.negotiated_profile.invalidation_engine = 1;
    manifest.negotiated_profile.fallback_decision = 1;
    memset(manifest.negotiated_profile.supported_memory_policies_digest, 0x77,
           sizeof(manifest.negotiated_profile.supported_memory_policies_digest));
    capability.physical_node_id = 17;
    capability.node_instance_id = 101;
    capability.profile_generation = 8;
    memset(capability.profile_digest, 0x88,
           sizeof(capability.profile_digest));
    manifest.negotiated_profile.per_node_capabilities.entries = &capability;
    manifest.negotiated_profile.per_node_capabilities.count = 1;
    manifest.negotiated_profile.per_node_capabilities.capacity = 1;
    memset(manifest.reservation_id, 0x99, sizeof(manifest.reservation_id));
    if (expect(wvm_node_runtime_manifest_validate(&manifest, error,
                                                  sizeof(error)) == 0,
               "validate runtime manifest") ||
        expect(wvm_runtime_manifest_profile_digest(
                   &manifest, profile_digest, error, sizeof(error)) == 0,
               "derive capability profile digest")) {
        return 1;
    }

    manifest_fd = mkstemp(manifest_path);
    if (manifest_fd < 0) {
        perror("mkstemp runtime manifest");
        return 1;
    }
    close(manifest_fd);
    unlink(manifest_path);
    wvm_runtime_manifest_storage_init(&loaded_storage);
    if (expect(wvm_runtime_manifest_file_publish(
                   manifest_path, &manifest, error, sizeof(error)) == 0 &&
                   wvm_runtime_manifest_load_file(
                       manifest_path, &loaded_storage, error,
                       sizeof(error)) == 0 &&
                   loaded_storage.manifest.vm_id == manifest.vm_id &&
                   loaded_storage.manifest.expected_node_instance_id ==
                       manifest.expected_node_instance_id &&
                   memcmp(loaded_storage.manifest.candidate_manifest_digest,
                          manifest.candidate_manifest_digest,
                          sizeof(manifest.candidate_manifest_digest)) == 0 &&
                   memcmp(loaded_storage.manifest.activation_fence,
                          manifest.activation_fence,
                          sizeof(manifest.activation_fence)) == 0,
               "atomically publish and reload admitted runtime manifest")) {
        wvm_runtime_manifest_storage_free(&loaded_storage);
        unlink(manifest_path);
        return 1;
    }
    wvm_runtime_manifest_storage_free(&loaded_storage);
    unlink(manifest_path);

    wvm_runtime_gate_init(&gate);
    if (expect(wvm_runtime_gate_prepare(&gate, &manifest, 17, 101, error,
                                        sizeof(error)) == 0,
               "prepare exact local manifest")) {
        return 1;
    }

    memset(&registration, 0, sizeof(registration));
    registration.connection_role = WVM_MANIFEST_ROLE_QEMU_FRONTEND;
    registration.vm_id = manifest.vm_id;
    registration.vm_incarnation = manifest.vm_incarnation;
    registration.manifest_generation = manifest.manifest_generation;
    memcpy(registration.candidate_manifest_digest,
           manifest.candidate_manifest_digest,
           sizeof(registration.candidate_manifest_digest));
    registration.local_runtime_instance_id = 1001;
    registration.caller_process_instance_id = 2001;
    memcpy(registration.capability_profile_digest, profile_digest,
           sizeof(registration.capability_profile_digest));
    strcpy(registration.requested_endpoint_name,
           manifest.local_names.namespace_name);
    if (expect(wvm_runtime_gate_register(&gate, &registration, &connection_id,
                                         error, sizeof(error)) == 0 &&
                   connection_id != 0,
               "register QEMU before activation") ||
        expect(wvm_runtime_gate_activate(&gate, manifest.activation_fence, error,
                                         sizeof(error)) == 0,
               "activate exact fence")) {
        return 1;
    }

    memset(&operation, 0, sizeof(operation));
    operation.connection_id = connection_id;
    operation.vm_id = manifest.vm_id;
    operation.vm_incarnation = manifest.vm_incarnation;
    operation.manifest_generation = manifest.manifest_generation;
    memcpy(operation.candidate_manifest_digest,
           manifest.candidate_manifest_digest,
           sizeof(operation.candidate_manifest_digest));
    operation.route_snapshot_key = manifest.required_route_snapshot_key;
    memcpy(operation.activation_fence, manifest.activation_fence,
           sizeof(operation.activation_fence));
    operation.operation_id[15] = 1;
    if (expect(wvm_runtime_gate_authorize(&gate, &operation, error,
                                          sizeof(error)) == 0,
               "authorize matching semantic operation")) {
        return 1;
    }
    operation.route_snapshot_key.route_generation++;
    if (expect(wvm_runtime_gate_authorize(&gate, &operation, error,
                                          sizeof(error)) != 0,
               "reject stale route generation")) {
        return 1;
    }
    if (expect(wvm_runtime_gate_revoke(&gate, connection_id, error,
                                       sizeof(error)) == 0 &&
                   wvm_runtime_gate_authorize(&gate, &operation, error,
                                              sizeof(error)) != 0,
               "reject revoked connection")) {
        return 1;
    }

    puts("runtime-gate tests: PASS");
    return 0;
}
