#include <stdio.h>
#include <string.h>

#include "wavevm_manifest.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "placement-plan test: %s\n", message);
        return -1;
    }
    return 0;
}

static void fill_id(uint8_t id[WVM_IDENTITY_ID_BYTES], uint8_t value)
{
    memset(id, 0, WVM_IDENTITY_ID_BYTES);
    id[WVM_IDENTITY_ID_BYTES - 1] = value;
}

int main(void)
{
    struct wvm_exclusive_lease host_leases[2];
    struct wvm_reservation_requirement requirements[2];
    struct wvm_vcpu_assignment vcpus[2];
    struct wvm_memory_chunk_assignment memory[2];
    struct wvm_placement_plan plan;
    struct wvm_exclusive_lease decoded_host_leases[2];
    struct wvm_reservation_requirement decoded_requirements[2];
    struct wvm_vcpu_assignment decoded_vcpus[2];
    struct wvm_memory_chunk_assignment decoded_memory[2];
    struct wvm_storage_assignment decoded_storage[1];
    struct wvm_placement_plan decoded_plan;
    uint8_t bytes[4096];
    uint8_t digest[WVM_SHA256_DIGEST_BYTES];
    size_t encoded_bytes;
    char error[256] = {0};

    memset(host_leases, 0, sizeof(host_leases));
    host_leases[0].lease_kind = 1;
    strcpy(host_leases[0].lease_name, "/run/wavevm/host-qemu.sock");
    host_leases[0].lease_generation = 1;

    memset(requirements, 0, sizeof(requirements));
    fill_id(requirements[0].reservation_id, 0x10);
    requirements[0].physical_node_id = 17;
    requirements[0].node_instance_id = 101;
    requirements[0].inventory_revision = 19;
    requirements[0].guest_vcpu_slots = 1;
    requirements[0].guest_memory_bytes = 2 * 1024 * 1024;
    requirements[0].overhead_vcpu_slots = 1;
    requirements[0].overhead_memory_bytes = 1024 * 1024;
    requirements[0].exclusive_leases.entries = host_leases;
    requirements[0].exclusive_leases.count = 1;
    requirements[0].exclusive_leases.capacity = 1;

    fill_id(requirements[1].reservation_id, 0x20);
    requirements[1].physical_node_id = 99;
    requirements[1].node_instance_id = 202;
    requirements[1].inventory_revision = 23;
    requirements[1].guest_vcpu_slots = 1;
    requirements[1].guest_memory_bytes = 2 * 1024 * 1024;

    memset(vcpus, 0, sizeof(vcpus));
    vcpus[0].guest_vcpu_index = 0;
    vcpus[0].executor_physical_node_id = 17;
    vcpus[0].backend = WVM_MANIFEST_BACKEND_TCG;
    vcpus[0].executor_class = 1;
    vcpus[0].executor_slot = 0;
    fill_id(vcpus[0].reservation_id, 0x10);
    vcpus[1] = vcpus[0];
    vcpus[1].guest_vcpu_index = 1;
    vcpus[1].executor_physical_node_id = 99;
    vcpus[1].executor_slot = 1;
    fill_id(vcpus[1].reservation_id, 0x20);

    memset(memory, 0, sizeof(memory));
    memory[0].gpa_start = 0;
    memory[0].bytes = 2 * 1024 * 1024;
    memory[0].directory_physical_node_id = 17;
    memory[0].executor_physical_node_id = 17;
    memory[0].consistency_policy = 1;
    fill_id(memory[0].reservation_id, 0x10);
    memory[1] = memory[0];
    memory[1].gpa_start = 2 * 1024 * 1024;
    memory[1].directory_physical_node_id = 99;
    memory[1].executor_physical_node_id = 99;
    fill_id(memory[1].reservation_id, 0x20);

    memset(&plan, 0, sizeof(plan));
    fill_id(plan.admission_tx_id, 0x42);
    memset(plan.eligibility_fence_digest, 0x51,
           sizeof(plan.eligibility_fence_digest));
    plan.inventory_revision = 23;
    plan.membership_revision = 7;
    plan.topology_revision = 11;
    plan.capability_profile_generation = 13;
    plan.host_node = 17;
    plan.vcpu_assignments.entries = vcpus;
    plan.vcpu_assignments.count = 2;
    plan.memory_assignments.entries = memory;
    plan.memory_assignments.count = 2;
    plan.reservation_requirements.entries = requirements;
    plan.reservation_requirements.count = 2;
    plan.guest_topology.topology_policy = WVM_MANIFEST_GUEST_TOPOLOGY_FLAT;
    plan.guest_topology.guest_numa_nodes = 1;
    plan.route_scope_key.vm_id = 256;
    plan.route_scope_key.vm_incarnation = 1;
    plan.route_scope_key.route_scope_id = 1;

    if (expect(wvm_placement_plan_encode(&plan, bytes, sizeof(bytes),
                                         &encoded_bytes, digest, error,
                                         sizeof(error)) == 0,
               "encode complete plan") ||
        expect(memcmp(bytes + 16, digest, sizeof(digest)) == 0,
               "plan self-digest is serialized")) {
        return 1;
    }

    memset(&decoded_plan, 0, sizeof(decoded_plan));
    decoded_plan.vcpu_assignments.entries = decoded_vcpus;
    decoded_plan.vcpu_assignments.capacity = 2;
    decoded_plan.memory_assignments.entries = decoded_memory;
    decoded_plan.memory_assignments.capacity = 2;
    decoded_plan.storage_assignments.entries = decoded_storage;
    decoded_plan.storage_assignments.capacity = 1;
    decoded_plan.reservation_requirements.entries = decoded_requirements;
    decoded_plan.reservation_requirements.capacity = 2;
    decoded_requirements[0].exclusive_leases.entries = decoded_host_leases;
    decoded_requirements[0].exclusive_leases.capacity = 2;

    if (expect(wvm_placement_plan_decode(bytes, encoded_bytes, &decoded_plan,
                                         error, sizeof(error)) == 0,
               "decode complete plan") ||
        expect(memcmp(decoded_plan.plan_digest, digest, sizeof(digest)) == 0,
               "decoded digest") ||
        expect(decoded_plan.host_node == 17, "decoded host") ||
        expect(decoded_plan.vcpu_assignments.count == 2,
               "decoded vCPU count") ||
        expect(decoded_plan.memory_assignments.count == 2,
               "decoded memory count") ||
        expect(decoded_plan.reservation_requirements.count == 2,
               "decoded reservation count") ||
        expect(decoded_requirements[0].exclusive_leases.count == 1,
               "decode nested lease") ||
        expect(strcmp(decoded_host_leases[0].lease_name,
                      host_leases[0].lease_name) == 0,
               "nested lease content")) {
        return 1;
    }

    bytes[16] ^= 0x01;
    if (expect(wvm_placement_plan_decode(bytes, encoded_bytes, &decoded_plan,
                                         error, sizeof(error)) != 0,
               "reject a mismatched plan digest")) {
        return 1;
    }

    host_leases[1] = host_leases[0];
    host_leases[1].lease_generation = 2;
    requirements[0].exclusive_leases.count = 2;
    requirements[0].exclusive_leases.capacity = 2;
    if (expect(wvm_placement_plan_encode(&plan, bytes, sizeof(bytes),
                                         &encoded_bytes, digest, error,
                                         sizeof(error)) != 0,
               "reject duplicate lease resource with a different generation")) {
        return 1;
    }

    puts("placement-plan tests: PASS");
    return 0;
}
