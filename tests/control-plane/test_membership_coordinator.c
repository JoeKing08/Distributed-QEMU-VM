#define _XOPEN_SOURCE 700

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wavevm_canonical.h"
#include "wavevm_membership_coordinator.h"

#define MIB (1024ULL * 1024ULL)

struct prepare_capture {
    unsigned int calls;
    unsigned int fail_on_call;
};

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "membership-coordinator test: %s\n", message);
        return -1;
    }
    return 0;
}

static int member_key_equal(const struct wvm_member_key *left,
                            const struct wvm_member_key *right)
{
    return left && right && left->role_type == right->role_type &&
           left->role_id == right->role_id &&
           left->instance_id == right->instance_id;
}

static int authorize(void *context,
                     enum wvm_membership_controller_authorization_action action,
                     const struct wvm_member_key *actor,
                     const struct wvm_member_key *subject, char *error,
                     size_t error_len)
{
    (void)context;
    (void)action;
    (void)error;
    (void)error_len;
    return member_key_equal(actor, subject) ? 0 : -1;
}

static void fill_endpoint(struct wvm_endpoint *endpoint, uint8_t last_octet,
                          uint16_t data_port, uint16_t control_port)
{
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->data_transport = WVM_DATA_TRANSPORT_UDP;
    endpoint->data_address_bytes = 4;
    endpoint->data_address[0] = 192;
    endpoint->data_address[1] = 0;
    endpoint->data_address[2] = 2;
    endpoint->data_address[3] = last_octet;
    endpoint->data_port = data_port;
    endpoint->control_transport = WVM_CONTROL_TRANSPORT_TLS_TCP;
    endpoint->control_port = control_port;
}

static void fill_capability(struct wvm_capability_ref *capability,
                            uint32_t node_id, uint64_t instance)
{
    memset(capability, 0, sizeof(*capability));
    capability->physical_node_id = node_id;
    capability->node_instance_id = instance;
    capability->profile_generation = 1;
    memset(capability->profile_digest, 0x21,
           sizeof(capability->profile_digest));
}

static void fill_node(struct wvm_node_record *node)
{
    memset(node, 0, sizeof(*node));
    node->physical_node_id = 18;
    node->node_instance_id = 102;
    node->failure_domain_id = 4;
    fill_endpoint(&node->control_endpoint, 18, 19200, 19201);
    fill_endpoint(&node->sidecar_endpoint, 18, 19220, 19221);
    node->role_bits = 1;
    node->pod_id = 1;
    node->local_vnode_first = 16;
    node->local_vnode_count = 16;
    node->inventory.physical_node_id = node->physical_node_id;
    node->inventory.node_instance_id = node->node_instance_id;
    node->inventory.failure_domain_id = node->failure_domain_id;
    node->inventory.inventory_revision = 1;
    node->inventory.registered_vcpu_slots = 8;
    node->inventory.registered_memory_bytes = 16 * MIB;
    node->inventory.reserved_host_cpu_slots = 1;
    node->inventory.reserved_host_memory_bytes = 1 * MIB;
    node->inventory.allocatable_vcpu_slots = 7;
    node->inventory.allocatable_memory_bytes = 15 * MIB;
    memset(node->inventory.storage_capabilities_digest, 0x11,
           sizeof(node->inventory.storage_capabilities_digest));
    memset(node->inventory.accelerator_fault_capabilities_digest, 0x12,
           sizeof(node->inventory.accelerator_fault_capabilities_digest));
    memset(node->inventory.exclusive_resource_inventory_digest, 0x13,
           sizeof(node->inventory.exclusive_resource_inventory_digest));
    fill_capability(&node->capability, node->physical_node_id,
                    node->node_instance_id);
}

static void fill_gateway(struct wvm_gateway_record *gateway)
{
    memset(gateway, 0, sizeof(*gateway));
    gateway->gateway_id = 7;
    gateway->gateway_instance_id = 701;
    gateway->hosting_physical_node_id = 17;
    gateway->failure_domain_id = 3;
    fill_endpoint(&gateway->endpoint, 17, 19120, 19121);
    gateway->role_bits = 1;
    gateway->pod_id_or_scope = 1;
}

static void node_key(const struct wvm_node_record *node,
                     struct wvm_member_key *key)
{
    memset(key, 0, sizeof(*key));
    key->role_type = WVM_MANIFEST_ROLE_NODE_RUNTIME;
    key->role_id = node->physical_node_id;
    key->instance_id = node->node_instance_id;
}

static void gateway_key(const struct wvm_gateway_record *gateway,
                        struct wvm_member_key *key)
{
    memset(key, 0, sizeof(*key));
    key->role_type = WVM_MANIFEST_ROLE_GATEWAY;
    key->role_id = gateway->gateway_id;
    key->instance_id = gateway->gateway_instance_id;
}

static int build_ack_set(struct wvm_required_ack_set *ack_set,
                         struct wvm_required_ack_entry *entries,
                         struct wvm_required_ack_entry *decoded_entries,
                         size_t entry_count,
                         const struct wvm_route_snapshot_key *route_key,
                         const struct wvm_member_key *first_member,
                         const struct wvm_endpoint *first_endpoint,
                         const struct wvm_member_key *second_member,
                         const struct wvm_endpoint *second_endpoint,
                         char *error, size_t error_len)
{
    struct wvm_required_ack_set source;
    uint8_t bytes[4096];
    size_t encoded_bytes;

    memset(entries, 0, entry_count * sizeof(*entries));
    entries[0].member_key = *first_member;
    entries[0].endpoint = *first_endpoint;
    entries[0].role_type = first_member->role_type;
    entries[0].expected_snapshot_key = *route_key;
    if (entry_count == 2) {
        entries[1].member_key = *second_member;
        entries[1].endpoint = *second_endpoint;
        entries[1].role_type = second_member->role_type;
        entries[1].expected_snapshot_key = *route_key;
    }
    memset(&source, 0, sizeof(source));
    source.entries.entries = entries;
    source.entries.count = entry_count;
    source.entries.capacity = entry_count;
    if (wvm_required_ack_set_encode(&source, bytes, sizeof(bytes),
                                    &encoded_bytes, error, error_len) != 0) {
        return -1;
    }
    memset(ack_set, 0, sizeof(*ack_set));
    ack_set->entries.entries = decoded_entries;
    ack_set->entries.capacity = entry_count;
    return wvm_required_ack_set_decode(bytes, encoded_bytes, ack_set, error,
                                       error_len);
}

static void build_route_key(struct wvm_route_snapshot_key *key,
                            uint64_t topology_revision,
                            uint64_t route_generation, uint8_t digest)
{
    memset(key, 0, sizeof(*key));
    key->scope_key.vm_id = 256;
    key->scope_key.vm_incarnation = 1;
    key->scope_key.route_scope_id = 1;
    key->topology_revision = topology_revision;
    key->route_generation = route_generation;
    memset(key->snapshot_digest, digest, sizeof(key->snapshot_digest));
}

static void build_transaction(struct wvm_route_transaction_record *transaction,
                              uint8_t operation_tail,
                              const struct wvm_route_snapshot_key *route_key,
                              const struct wvm_required_ack_set *ack_set)
{
    memset(transaction, 0, sizeof(*transaction));
    transaction->operation_id[WVM_IDENTITY_ID_BYTES - 1] = operation_tail;
    transaction->route_snapshot_key = *route_key;
    transaction->required_ack_set = *ack_set;
    transaction->operation_retention_horizon_ms = 5000;
    transaction->state = WVM_ROUTE_TRANSACTION_PREPARING;
}

static int route_prepare(void *context,
                         const struct wvm_route_transaction_record *transaction,
                         const struct wvm_required_ack_entry *ack_entry,
                         char *error, size_t error_len)
{
    struct prepare_capture *capture = context;

    (void)transaction;
    (void)ack_entry;
    capture->calls++;
    if (capture->fail_on_call == capture->calls) {
        snprintf(error, error_len, "simulated participant prepare failure");
        return -1;
    }
    return 0;
}

static int activate_gateway(struct wvm_membership_controller *controller,
                            const struct wvm_gateway_record *gateway,
                            const struct wvm_member_key *gateway_member,
                            char *error, size_t error_len)
{
    struct wvm_route_snapshot_key route_key;
    struct wvm_required_ack_entry ack_entries[1];
    struct wvm_required_ack_entry decoded_ack_entries[1];
    struct wvm_required_ack_set ack_set;
    struct wvm_route_transaction_record transaction;

    if (wvm_membership_controller_register_gateway(
            controller, gateway_member, gateway, error, error_len) != 0 ||
        wvm_membership_controller_begin_validation(controller, gateway_member,
                                                   error, error_len) != 0 ||
        wvm_membership_controller_report_self_health(
            controller, gateway_member, WVM_MEMBERSHIP_HEALTHY, error,
            error_len) != 0 ||
        wvm_membership_controller_prepare_member(controller, gateway_member,
                                                  error, error_len) != 0) {
        return -1;
    }
    build_route_key(&route_key, controller->topology_revision, 1, 0x51);
    if (build_ack_set(&ack_set, ack_entries, decoded_ack_entries, 1, &route_key,
                      gateway_member, &gateway->endpoint, NULL, NULL, error,
                      error_len) != 0) {
        return -1;
    }
    build_transaction(&transaction, 0x41, &route_key, &ack_set);
    return wvm_membership_controller_route_begin(controller, &transaction,
                                                 error, error_len) ||
                   wvm_membership_controller_route_ack_prepare(
                       controller, transaction.operation_id, gateway_member,
                       error, error_len) ||
                   wvm_membership_controller_route_commit(
                       controller, transaction.operation_id, error, error_len) ||
                   wvm_membership_controller_activate_member(
                       controller, gateway_member, transaction.operation_id,
                       error, error_len)
               ? -1
               : 0;
}

int main(void)
{
    char journal_path[] = "/tmp/wavevm-membership-coordinator.XXXXXX";
    struct wvm_membership_controller_member_entry members[4];
    struct wvm_membership_controller_member_entry recovered_members[4];
    struct wvm_membership_controller_route_entry routes[4];
    struct wvm_membership_controller_route_entry recovered_routes[4];
    struct wvm_membership_dependency dependencies[4];
    struct wvm_membership_dependency recovered_dependencies[4];
    struct wvm_membership_controller controller;
    struct wvm_membership_controller recovered_controller;
    struct wvm_gateway_record gateway;
    struct wvm_node_record node;
    struct wvm_member_key gateway_member;
    struct wvm_member_key node_member;
    struct wvm_route_snapshot_key route_key;
    struct wvm_required_ack_entry ack_entries[2];
    struct wvm_required_ack_entry decoded_ack_entries[2];
    struct wvm_required_ack_set ack_set;
    struct wvm_route_transaction_record failed_transaction;
    struct wvm_route_transaction_record successful_transaction;
    struct wvm_membership_join_request join_request;
    struct prepare_capture capture;
    const struct wvm_membership_controller_member_entry *entry;
    uint16_t route_state;
    char error[256] = {0};
    int fd;

    fd = mkstemp(journal_path);
    if (fd < 0) {
        perror("mkstemp");
        return 1;
    }
    close(fd);
    fill_gateway(&gateway);
    fill_node(&node);
    gateway_key(&gateway, &gateway_member);
    node_key(&node, &node_member);
    wvm_membership_controller_init(&controller, members, 4, routes, 4,
                                   dependencies, 4, authorize, NULL);
    if (expect(wvm_membership_controller_open(&controller, journal_path, error,
                                              sizeof(error)) == 0,
               "open membership controller") ||
        expect(activate_gateway(&controller, &gateway, &gateway_member, error,
                                sizeof(error)) == 0,
               "activate existing route participant")) {
        wvm_membership_controller_close(&controller);
        unlink(journal_path);
        return 1;
    }

    build_route_key(&route_key, controller.topology_revision + 1, 2, 0x52);
    if (expect(build_ack_set(&ack_set, ack_entries, decoded_ack_entries, 2,
                             &route_key, &node_member,
                             &node.control_endpoint, &gateway_member,
                             &gateway.endpoint, error, sizeof(error)) == 0,
               "build join RequiredAckSet")) {
        wvm_membership_controller_close(&controller);
        unlink(journal_path);
        return 1;
    }
    build_transaction(&failed_transaction, 0x42, &route_key, &ack_set);
    memset(&join_request, 0, sizeof(join_request));
    join_request.member_kind = WVM_MEMBERSHIP_COMPUTE;
    join_request.authenticated_actor = &node_member;
    join_request.node = &node;
    join_request.route_transaction = &failed_transaction;
    join_request.route_prepare = route_prepare;
    join_request.route_prepare_context = &capture;
    capture.calls = 0;
    capture.fail_on_call = 2;
    if (expect(wvm_membership_coordinator_join(&controller, &join_request,
                                               error, sizeof(error)) != 0,
               "abort join after a participant rejects prepare") ||
        expect(wvm_membership_controller_route_state(
                   &controller, failed_transaction.operation_id, &route_state,
                   error, sizeof(error)) == 0 &&
                   route_state == WVM_ROUTE_TRANSACTION_ABORTED,
               "failed join records an aborted route") ||
        expect((entry = wvm_membership_controller_find(&controller, &node_member)) !=
                   NULL &&
                   entry->node.desired_membership_state ==
                       WVM_MANIFEST_MEMBER_PREPARED,
               "failed join leaves the node non-schedulable")) {
        wvm_membership_controller_close(&controller);
        unlink(journal_path);
        return 1;
    }

    build_transaction(&successful_transaction, 0x43, &route_key, &ack_set);
    join_request.route_transaction = &successful_transaction;
    capture.calls = 0;
    capture.fail_on_call = 0;
    if (expect(wvm_membership_coordinator_join(&controller, &join_request,
                                               error, sizeof(error)) == 0,
               "join completes after every participant ACK") ||
        expect((entry = wvm_membership_controller_find(&controller, &node_member)) !=
                   NULL &&
                   entry->node.desired_membership_state ==
                       WVM_MANIFEST_MEMBER_ACTIVE &&
                   entry->has_activation_route_operation_id &&
                   memcmp(entry->activation_route_operation_id,
                          successful_transaction.operation_id,
                          WVM_IDENTITY_ID_BYTES) == 0,
               "active member retains its committed route authorization")) {
        wvm_membership_controller_close(&controller);
        unlink(journal_path);
        return 1;
    }
    wvm_membership_controller_close(&controller);

    wvm_membership_controller_init(&recovered_controller, recovered_members, 4,
                                   recovered_routes, 4, recovered_dependencies,
                                   4, authorize, NULL);
    if (expect(wvm_membership_controller_open(&recovered_controller,
                                              journal_path, error,
                                              sizeof(error)) == 0,
               "recover membership controller") ||
        expect((entry = wvm_membership_controller_find(&recovered_controller,
                                                        &node_member)) != NULL &&
                   entry->node.desired_membership_state ==
                       WVM_MANIFEST_MEMBER_ACTIVE &&
                   entry->has_activation_route_operation_id &&
                   memcmp(entry->activation_route_operation_id,
                          successful_transaction.operation_id,
                          WVM_IDENTITY_ID_BYTES) == 0,
               "recover active member route authorization") ||
        expect((capture.calls = 0,
                wvm_membership_coordinator_join(&recovered_controller,
                                                &join_request, error,
                                                sizeof(error)) == 0 &&
                capture.calls == 0),
               "replay skips already committed participant prepares")) {
        wvm_membership_controller_close(&recovered_controller);
        unlink(journal_path);
        return 1;
    }
    wvm_membership_controller_close(&recovered_controller);
    unlink(journal_path);
    puts("membership-coordinator tests: PASS");
    return 0;
}
