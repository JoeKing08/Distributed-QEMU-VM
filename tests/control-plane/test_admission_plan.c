#include <stdio.h>
#include <string.h>

#include "wavevm_admission.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "admission-plan test: %s\n", message);
        return -1;
    }
    return 0;
}

static struct wvm_admission_node make_node(uint32_t id, uint64_t instance,
                                           uint64_t inventory_revision,
                                           uint32_t vcpus, uint64_t memory)
{
    struct wvm_admission_node node;

    memset(&node, 0, sizeof(node));
    node.physical_node_id = id;
    node.node_instance_id = instance;
    node.inventory_revision = inventory_revision;
    node.membership_state = WVM_ADMISSION_MEMBER_ACTIVE;
    node.health_state = WVM_ADMISSION_HEALTHY;
    node.backend_capabilities =
        WVM_ADMISSION_BACKEND_CAP_KVM | WVM_ADMISSION_BACKEND_CAP_TCG;
    node.registered_vcpu_slots = vcpus + 1;
    node.registered_memory_bytes = memory + WVM_ADMISSION_PAGE_BYTES;
    node.reserved_host_vcpu_slots = 1;
    node.reserved_host_memory_bytes = WVM_ADMISSION_PAGE_BYTES;
    node.allocatable_vcpu_slots = vcpus;
    node.allocatable_memory_bytes = memory;
    return node;
}

static void fill_valid_plan(struct wvm_admission_plan *plan)
{
    memset(plan, 0, sizeof(*plan));
    plan->admission_tx_id[WVM_ADMISSION_ID_BYTES - 1] = 1;
    plan->membership_revision = 7;
    plan->topology_revision = 11;
    plan->capability_profile_generation = 13;
    plan->host_physical_node_id = 17;
    plan->reservation_count = 2;

    plan->reservations[0].physical_node_id = 17;
    plan->reservations[0].expected_node_instance_id = 101;
    plan->reservations[0].expected_inventory_revision = 19;
    plan->reservations[0].guest_vcpu_slots = 2;
    plan->reservations[0].guest_memory_bytes = 4 * 1024 * 1024;
    plan->reservations[0].host_overhead_vcpu_slots = 1;
    plan->reservations[0].host_overhead_memory_bytes = 1 * 1024 * 1024;

    plan->reservations[1].physical_node_id = 99;
    plan->reservations[1].expected_node_instance_id = 202;
    plan->reservations[1].expected_inventory_revision = 23;
    plan->reservations[1].guest_vcpu_slots = 1;
    plan->reservations[1].guest_memory_bytes = 4 * 1024 * 1024;
}

int main(void)
{
    struct wvm_admission_snapshot snapshot;
    struct wvm_admission_request request;
    struct wvm_admission_plan plan;
    struct wvm_admission_plan proposed_plan;
    struct wvm_admission_placement_options options;
    struct wvm_vcpu_assignment vcpu_assignments[3];
    struct wvm_memory_chunk_assignment memory_assignments[4];
    struct wvm_reservation_requirement reservation_requirements[2];
    struct wvm_placement_plan placement_plan;
    uint8_t admission_tx_id[WVM_ADMISSION_ID_BYTES] = {0};
    uint8_t eligibility_fence_digest[WVM_SHA256_DIGEST_BYTES];
    uint8_t placement_bytes[8192];
    uint8_t placement_digest[WVM_SHA256_DIGEST_BYTES];
    size_t placement_bytes_used;
    char error[256] = {0};

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.inventory_revision = 29;
    snapshot.membership_revision = 7;
    snapshot.topology_revision = 11;
    snapshot.capability_profile_generation = 13;
    snapshot.node_count = 2;
    snapshot.nodes[0] = make_node(17, 101, 19, 3, 8 * 1024 * 1024);
    snapshot.nodes[1] = make_node(99, 202, 23, 2, 8 * 1024 * 1024);

    memset(&request, 0, sizeof(request));
    request.vm_id = 256;
    request.vm_incarnation = 1;
    request.manifest_generation = 1;
    request.backend = WVM_ADMISSION_BACKEND_TCG;
    request.placement_policy = WVM_ADMISSION_PLACEMENT_SPREAD;
    request.requested_vcpu_slots = 3;
    request.requested_memory_bytes = 8 * 1024 * 1024;
    request.memory_chunk_bytes = 2 * 1024 * 1024;
    request.host_overhead_vcpu_slots = 1;
    request.host_overhead_memory_bytes = 1 * 1024 * 1024;

    fill_valid_plan(&plan);
    if (expect(wvm_admission_plan_validate(&snapshot, &request, &plan, error,
                                           sizeof(error)) == 0,
               "accept a complete, revision-pinned TCG plan")) {
        return 1;
    }

    admission_tx_id[WVM_ADMISSION_ID_BYTES - 1] = 0x44;
    if (expect(wvm_admission_plan_propose(&snapshot, &request, admission_tx_id,
                                          &proposed_plan, error,
                                          sizeof(error)) == 0,
               "generate a deterministic reservation plan") ||
        expect(proposed_plan.host_physical_node_id == 17,
               "choose an explicit compact host") ||
        expect(proposed_plan.reservation_count == 2,
               "use both nodes when local CPU capacity is insufficient") ||
        expect(proposed_plan.reservations[0].physical_node_id == 17 &&
                   proposed_plan.reservations[1].physical_node_id == 99,
               "emit reservations in stable node order")) {
        return 1;
    }

    memset(&options, 0, sizeof(options));
    options.memory_consistency_policy = 1;
    options.guest_topology_policy = WVM_MANIFEST_GUEST_TOPOLOGY_FLAT;
    options.guest_numa_nodes = 1;
    options.executor_class = 1;
    options.route_scope_key.vm_id = request.vm_id;
    options.route_scope_key.vm_incarnation = request.vm_incarnation;
    options.route_scope_key.route_scope_id = 1;
    memset(eligibility_fence_digest, 0x5a, sizeof(eligibility_fence_digest));
    memset(&placement_plan, 0, sizeof(placement_plan));
    placement_plan.vcpu_assignments.entries = vcpu_assignments;
    placement_plan.vcpu_assignments.capacity =
        sizeof(vcpu_assignments) / sizeof(vcpu_assignments[0]);
    placement_plan.memory_assignments.entries = memory_assignments;
    placement_plan.memory_assignments.capacity =
        sizeof(memory_assignments) / sizeof(memory_assignments[0]);
    placement_plan.reservation_requirements.entries = reservation_requirements;
    placement_plan.reservation_requirements.capacity =
        sizeof(reservation_requirements) / sizeof(reservation_requirements[0]);
    if (expect(wvm_admission_placement_plan_build(
                   &snapshot, &request, &proposed_plan, eligibility_fence_digest,
                   &options, &placement_plan, error, sizeof(error)) == 0,
               "materialize canonical placement assignments") ||
        expect(placement_plan.vcpu_assignments.count == 3 &&
                   placement_plan.memory_assignments.count == 4,
               "cover every requested CPU and memory chunk") ||
        expect(placement_plan.host_node == proposed_plan.host_physical_node_id,
               "preserve selected host") ||
        expect(wvm_placement_plan_encode(
                   &placement_plan, placement_bytes, sizeof(placement_bytes),
                   &placement_bytes_used, placement_digest, error,
                   sizeof(error)) == 0,
               "encode the generated placement plan")) {
        return 1;
    }

    snapshot.nodes[1].committed_vcpu_slots = 2;
    if (expect(wvm_admission_plan_propose(&snapshot, &request, admission_tx_id,
                                          &proposed_plan, error,
                                          sizeof(error)) != 0,
               "reject a request that collides with committed capacity")) {
        return 1;
    }
    snapshot.nodes[1].committed_vcpu_slots = 0;

    plan.reservations[1].expected_inventory_revision++;
    if (expect(wvm_admission_plan_validate(&snapshot, &request, &plan, error,
                                           sizeof(error)) != 0,
               "reject stale inventory revision")) {
        return 1;
    }

    fill_valid_plan(&plan);
    plan.reservations[1].host_overhead_memory_bytes = WVM_ADMISSION_PAGE_BYTES;
    if (expect(wvm_admission_plan_validate(&snapshot, &request, &plan, error,
                                           sizeof(error)) != 0,
               "reject host overhead on a non-host")) {
        return 1;
    }

    fill_valid_plan(&plan);
    snapshot.nodes[1].membership_state = WVM_ADMISSION_MEMBER_CORDONED;
    if (expect(wvm_admission_plan_validate(&snapshot, &request, &plan, error,
                                           sizeof(error)) != 0,
               "reject cordoned member")) {
        return 1;
    }

    snapshot.nodes[1].membership_state = WVM_ADMISSION_MEMBER_ACTIVE;
    snapshot.nodes[1].backend_capabilities = WVM_ADMISSION_BACKEND_CAP_KVM;
    if (expect(wvm_admission_plan_validate(&snapshot, &request, &plan, error,
                                           sizeof(error)) != 0,
               "reject backend-incompatible member")) {
        return 1;
    }

    snapshot.nodes[1].backend_capabilities =
        WVM_ADMISSION_BACKEND_CAP_KVM | WVM_ADMISSION_BACKEND_CAP_TCG;
    request.vm_id = 0;
    if (expect(wvm_admission_plan_validate(&snapshot, &request, &plan, error,
                                           sizeof(error)) != 0,
               "reject legacy VM namespace in V1 admission")) {
        return 1;
    }

    puts("admission-plan tests: PASS");
    return 0;
}
