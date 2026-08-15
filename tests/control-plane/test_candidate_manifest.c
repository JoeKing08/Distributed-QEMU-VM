#include <stdio.h>
#include <string.h>

#include "wavevm_manifest.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "candidate-manifest test: %s\n", message);
        return -1;
    }
    return 0;
}

static void fill_id(uint8_t id[WVM_IDENTITY_ID_BYTES], uint8_t value)
{
    memset(id, 0, WVM_IDENTITY_ID_BYTES);
    id[WVM_IDENTITY_ID_BYTES - 1] = value;
}

static void fill_capability(struct wvm_capability_ref *capability,
                            uint32_t physical_node_id,
                            uint64_t node_instance_id, uint8_t digest_byte)
{
    memset(capability, 0, sizeof(*capability));
    capability->physical_node_id = physical_node_id;
    capability->node_instance_id = node_instance_id;
    capability->profile_generation = 13;
    memset(capability->profile_digest, digest_byte,
           sizeof(capability->profile_digest));
}

int main(void)
{
    struct wvm_reservation_requirement requirements[2];
    struct wvm_vcpu_assignment vcpus[2];
    struct wvm_memory_chunk_assignment memory[2];
    struct wvm_placement_plan plan;
    struct wvm_capability_ref capabilities[2];
    struct wvm_required_member members[2];
    struct wvm_local_name_identity local_identity;
    struct wvm_candidate_vm_manifest candidate;
    struct wvm_capability_ref decoded_profile_caps[2];
    struct wvm_vcpu_assignment decoded_vcpus[2];
    struct wvm_memory_chunk_assignment decoded_memory[2];
    struct wvm_required_member decoded_members[2];
    struct wvm_capability_ref decoded_required_caps[2];
    struct wvm_reservation_requirement decoded_requirements[2];
    struct wvm_candidate_vm_manifest decoded_candidate;
    uint8_t plan_bytes[4096];
    uint8_t candidate_bytes[8192];
    uint8_t plan_digest[WVM_SHA256_DIGEST_BYTES];
    uint8_t candidate_digest[WVM_SHA256_DIGEST_BYTES];
    size_t plan_bytes_used;
    size_t candidate_bytes_used;
    char error[256] = {0};

    memset(requirements, 0, sizeof(requirements));
    fill_id(requirements[0].reservation_id, 0x10);
    requirements[0].physical_node_id = 17;
    requirements[0].node_instance_id = 101;
    requirements[0].inventory_revision = 19;
    requirements[0].guest_vcpu_slots = 1;
    requirements[0].guest_memory_bytes = 2 * 1024 * 1024;
    requirements[0].overhead_vcpu_slots = 1;
    requirements[0].overhead_memory_bytes = 1024 * 1024;
    fill_id(requirements[1].reservation_id, 0x20);
    requirements[1].physical_node_id = 99;
    requirements[1].node_instance_id = 202;
    requirements[1].inventory_revision = 23;
    requirements[1].guest_vcpu_slots = 1;
    requirements[1].guest_memory_bytes = 2 * 1024 * 1024;

    memset(vcpus, 0, sizeof(vcpus));
    vcpus[0].executor_physical_node_id = 17;
    vcpus[0].backend = WVM_MANIFEST_BACKEND_TCG;
    vcpus[0].executor_class = 1;
    fill_id(vcpus[0].reservation_id, 0x10);
    vcpus[1] = vcpus[0];
    vcpus[1].guest_vcpu_index = 1;
    vcpus[1].executor_physical_node_id = 99;
    vcpus[1].executor_slot = 1;
    fill_id(vcpus[1].reservation_id, 0x20);

    memset(memory, 0, sizeof(memory));
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
    if (expect(wvm_placement_plan_encode(&plan, plan_bytes, sizeof(plan_bytes),
                                         &plan_bytes_used, plan_digest, error,
                                         sizeof(error)) == 0,
               "encode plan")) {
        return 1;
    }
    memcpy(plan.plan_digest, plan_digest, sizeof(plan.plan_digest));

    fill_capability(&capabilities[0], 17, 101, 0x61);
    fill_capability(&capabilities[1], 99, 202, 0x62);
    memset(members, 0, sizeof(members));
    members[0].member_key.role_type = WVM_MANIFEST_ROLE_NODE_RUNTIME;
    members[0].member_key.role_id = 17;
    members[0].member_key.instance_id = 1001;
    members[0].physical_node_id = 17;
    members[0].failure_domain_id = 1;
    members[0].capability = capabilities[0];
    members[0].required_state = WVM_MANIFEST_MEMBER_ACTIVE;
    members[1] = members[0];
    members[1].member_key.role_id = 99;
    members[1].member_key.instance_id = 1002;
    members[1].physical_node_id = 99;
    members[1].failure_domain_id = 2;
    members[1].capability = capabilities[1];

    memset(&candidate, 0, sizeof(candidate));
    fill_id(candidate.manifest_id, 0x01);
    candidate.manifest_schema_version = 1;
    candidate.vm_id = 256;
    candidate.vm_incarnation = 1;
    candidate.manifest_generation = 1;
    fill_id(candidate.request_id, 0x02);
    memcpy(candidate.admission_tx_id, plan.admission_tx_id,
           sizeof(candidate.admission_tx_id));
    memcpy(candidate.eligibility_fence_digest, plan.eligibility_fence_digest,
           sizeof(candidate.eligibility_fence_digest));
    candidate.candidate_created_at = 1;
    strcpy(candidate.guest_machine.architecture, "x86_64");
    strcpy(candidate.guest_machine.machine_type, "pc-i440fx-5.2");
    candidate.guest_machine.qemu_compat_version = 502;
    candidate.guest_machine.firmware_policy = 1;
    candidate.guest_topology = plan.guest_topology;
    candidate.execution_plan.backend = WVM_MANIFEST_BACKEND_TCG;
    candidate.execution_plan.context_schema_version = 1;
    candidate.execution_plan.dirty_capture_engine = 1;
    candidate.execution_plan.read_fault_engine = 1;
    candidate.execution_plan.invalidation_engine = 1;
    candidate.execution_plan.per_node_capabilities.entries = capabilities;
    candidate.execution_plan.per_node_capabilities.count = 2;
    memset(candidate.execution_plan.supported_memory_policies_digest, 0x71,
           sizeof(candidate.execution_plan.supported_memory_policies_digest));
    candidate.execution_plan.fallback_decision = 1;
    candidate.consistency_policy.dirty_batch_size = 1;
    candidate.consistency_policy.handoff_commit_policy = 1;
    candidate.consistency_policy.subscriber_delivery_policy = 1;
    candidate.consistency_policy.max_commit_latency_ms = 1000;
    memset(candidate.storage_device_plan.qemu_device_configuration_digest, 0x81,
           sizeof(candidate.storage_device_plan.qemu_device_configuration_digest));
    candidate.host_node = 17;
    candidate.vcpu_placements.entries = vcpus;
    candidate.vcpu_placements.count = 2;
    candidate.memory_placements.entries = memory;
    candidate.memory_placements.count = 2;
    candidate.required_members.entries = members;
    candidate.required_members.count = 2;
    candidate.required_capabilities.entries = capabilities;
    candidate.required_capabilities.count = 2;
    candidate.reservation_requirements.entries = requirements;
    candidate.reservation_requirements.count = 2;
    candidate.route_scope_key = plan.route_scope_key;
    candidate.prepared_route_snapshot_key.scope_key = plan.route_scope_key;
    candidate.prepared_route_snapshot_key.topology_revision = 11;
    candidate.prepared_route_snapshot_key.route_generation = 1;
    memset(candidate.prepared_route_snapshot_key.snapshot_digest, 0x91,
           sizeof(candidate.prepared_route_snapshot_key.snapshot_digest));
    memcpy(candidate.plan_digest, plan_digest, sizeof(candidate.plan_digest));
    memset(&local_identity, 0, sizeof(local_identity));
    local_identity.vm_id = candidate.vm_id;
    local_identity.vm_incarnation = candidate.vm_incarnation;
    local_identity.manifest_generation = candidate.manifest_generation;
    local_identity.physical_node_id = candidate.host_node;
    memcpy(local_identity.manifest_id, candidate.manifest_id,
           sizeof(local_identity.manifest_id));
    memcpy(local_identity.admission_tx_id, candidate.admission_tx_id,
           sizeof(local_identity.admission_tx_id));
    if (expect(wvm_local_name_namespace_derive(
                   &local_identity, &candidate.local_name_namespace, error,
                   sizeof(error)) == 0,
               "derive candidate namespace")) {
        return 1;
    }
    candidate.lifecycle_policy.start_policy = 1;
    candidate.lifecycle_policy.failure_policy = 1;
    candidate.lifecycle_policy.completion_query_horizon_ms = 5000;
    candidate.lifecycle_policy.route_retention_horizon_ms = 6000;
    candidate.namespace_abi = WVM_MANIFEST_NAMESPACE_V1_U32;

    if (expect(wvm_candidate_vm_manifest_encode(
                   &candidate, candidate_bytes, sizeof(candidate_bytes),
                   &candidate_bytes_used, candidate_digest, error,
                   sizeof(error)) == 0,
               "encode candidate") ||
        expect(memcmp(candidate_bytes + 50, candidate_digest,
                      sizeof(candidate_digest)) == 0,
               "candidate self-digest is serialized")) {
        return 1;
    }
    memcpy(candidate.manifest_digest, candidate_digest,
           sizeof(candidate.manifest_digest));
    if (expect(wvm_candidate_vm_manifest_matches_plan(&candidate, &plan, error,
                                                       sizeof(error)) == 0,
               "candidate matches plan")) {
        return 1;
    }

    memset(&decoded_candidate, 0, sizeof(decoded_candidate));
    decoded_candidate.execution_plan.per_node_capabilities.entries =
        decoded_profile_caps;
    decoded_candidate.execution_plan.per_node_capabilities.capacity = 2;
    decoded_candidate.vcpu_placements.entries = decoded_vcpus;
    decoded_candidate.vcpu_placements.capacity = 2;
    decoded_candidate.memory_placements.entries = decoded_memory;
    decoded_candidate.memory_placements.capacity = 2;
    decoded_candidate.required_members.entries = decoded_members;
    decoded_candidate.required_members.capacity = 2;
    decoded_candidate.required_capabilities.entries = decoded_required_caps;
    decoded_candidate.required_capabilities.capacity = 2;
    decoded_candidate.reservation_requirements.entries = decoded_requirements;
    decoded_candidate.reservation_requirements.capacity = 2;
    if (expect(wvm_candidate_vm_manifest_decode(
                   candidate_bytes, candidate_bytes_used, &decoded_candidate,
                   error, sizeof(error)) == 0,
               "decode candidate") ||
        expect(memcmp(decoded_candidate.manifest_digest, candidate_digest,
                      sizeof(candidate_digest)) == 0,
               "decoded candidate digest") ||
        expect(decoded_candidate.required_members.count == 2,
               "decoded members") ||
        expect(decoded_candidate.execution_plan.per_node_capabilities.count == 2,
               "decoded profile capabilities") ||
        expect(decoded_candidate.vcpu_placements.entries[1]
                       .executor_physical_node_id == 99,
               "decoded remote vCPU placement")) {
        return 1;
    }

    candidate_bytes[50] ^= 0x01;
    if (expect(wvm_candidate_vm_manifest_decode(
                   candidate_bytes, candidate_bytes_used, &decoded_candidate,
                   error, sizeof(error)) != 0,
               "reject candidate digest corruption")) {
        return 1;
    }

    puts("candidate-manifest tests: PASS");
    return 0;
}
