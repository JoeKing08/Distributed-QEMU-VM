#include <stdio.h>
#include <string.h>

#include "wavevm_route_runtime.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "route-runtime test: %s\n", message);
        return -1;
    }
    return 0;
}

static void fill_endpoint(struct wvm_endpoint *endpoint, uint8_t last_octet,
                          uint16_t port)
{
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->data_transport = WVM_DATA_TRANSPORT_UDP;
    endpoint->data_address_bytes = 4;
    endpoint->data_address[0] = 192;
    endpoint->data_address[1] = 0;
    endpoint->data_address[2] = 2;
    endpoint->data_address[3] = last_octet;
    endpoint->data_port = port;
    endpoint->control_transport = WVM_CONTROL_TRANSPORT_TLS_TCP;
    endpoint->control_port = (uint16_t)(port + 1000U);
}

static void fill_rule(struct wvm_route_rule_record *rule,
                      uint16_t destination_kind, uint64_t destination_scope,
                      uint32_t vnode, uint8_t address_tail, uint16_t port,
                      uint16_t hop_limit)
{
    memset(rule, 0, sizeof(*rule));
    rule->destination_kind = destination_kind;
    rule->destination_scope = destination_scope;
    rule->destination_vnode_or_endpoint = vnode;
    rule->next_hop_kind = WVM_ROUTE_NEXT_HOP_ENDPOINT;
    rule->next_hop_member.role_type = WVM_MANIFEST_ROLE_NODE_RUNTIME;
    rule->next_hop_member.role_id = address_tail;
    rule->next_hop_member.instance_id = 1000U + address_tail;
    fill_endpoint(&rule->next_hop_endpoint, address_tail, port);
    rule->hop_limit = hop_limit;
}

static int finalize_snapshot(struct wvm_route_snapshot_record *snapshot,
                             struct wvm_route_rule_record *rules,
                             size_t rule_count,
                             struct wvm_required_ack_entry *acks,
                             size_t ack_count, uint32_t vm_id,
                             uint64_t incarnation, uint64_t route_scope_id,
                             uint64_t topology_revision,
                             uint64_t route_generation,
                             uint16_t topology_kind, char *error,
                             size_t error_len)
{
    uint8_t bytes[16384];
    uint8_t digest[WVM_SHA256_DIGEST_BYTES];
    size_t encoded_bytes;
    size_t i;

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->route_snapshot_key.scope_key.vm_id = vm_id;
    snapshot->route_snapshot_key.scope_key.vm_incarnation = incarnation;
    snapshot->route_snapshot_key.scope_key.route_scope_id = route_scope_id;
    snapshot->route_snapshot_key.topology_revision = topology_revision;
    snapshot->route_snapshot_key.route_generation = route_generation;
    snapshot->membership_revision = topology_revision;
    snapshot->topology_kind = topology_kind;
    snapshot->next_hop_rules.entries = rules;
    snapshot->next_hop_rules.count = rule_count;
    snapshot->next_hop_rules.capacity = rule_count;
    snapshot->required_ack_set.entries.entries = acks;
    snapshot->required_ack_set.entries.count = ack_count;
    snapshot->required_ack_set.entries.capacity = ack_count;
    snapshot->operation_retention_horizon_ms = 5000;
    snapshot->retirement_policy = 1;

    for (i = 0; i < ack_count; i++) {
        memset(&acks[i], 0, sizeof(acks[i]));
        acks[i].member_key = rules[0].next_hop_member;
        acks[i].endpoint = rules[0].next_hop_endpoint;
        acks[i].role_type = WVM_MANIFEST_ROLE_NODE_RUNTIME;
        acks[i].expected_snapshot_key = snapshot->route_snapshot_key;
    }
    if (wvm_route_snapshot_record_encode(snapshot, bytes, sizeof(bytes),
                                         &encoded_bytes, digest, error,
                                         error_len) != 0) {
        return -1;
    }
    memcpy(snapshot->route_snapshot_key.snapshot_digest, digest, sizeof(digest));
    for (i = 0; i < ack_count; i++) {
        memcpy(acks[i].expected_snapshot_key.snapshot_digest, digest,
               sizeof(digest));
    }
    return wvm_route_snapshot_record_validate(snapshot, error, error_len);
}

static void envelope_for_snapshot(struct wvm_envelope_v1 *envelope,
                                  const struct wvm_route_snapshot_key *key,
                                  uint16_t destination_kind,
                                  uint64_t destination_scope, uint32_t vnode)
{
    memset(envelope, 0, sizeof(*envelope));
    envelope->vm_id = key->scope_key.vm_id;
    envelope->vm_incarnation = key->scope_key.vm_incarnation;
    envelope->route_scope_id = key->scope_key.route_scope_id;
    envelope->topology_revision = key->topology_revision;
    envelope->route_generation = key->route_generation;
    memcpy(envelope->route_snapshot_digest, key->snapshot_digest,
           sizeof(envelope->route_snapshot_digest));
    envelope->route.destination_kind = destination_kind;
    envelope->route.destination_scope = destination_scope;
    envelope->route.destination_vnode_or_endpoint = vnode;
    envelope->route.hop_limit = 8;
    envelope->route.hop_count = 0;
}

int main(void)
{
    struct wvm_route_runtime runtime;
    struct wvm_route_snapshot_record flat;
    struct wvm_route_snapshot_record successor;
    struct wvm_route_snapshot_record fractal;
    struct wvm_route_rule_record flat_rules[1];
    struct wvm_route_rule_record successor_rules[1];
    struct wvm_route_rule_record fractal_rules[2];
    struct wvm_required_ack_entry flat_acks[1];
    struct wvm_required_ack_entry successor_acks[1];
    struct wvm_required_ack_entry fractal_acks[1];
    struct wvm_envelope_v1 envelope;
    struct wvm_route_runtime_next_hop next_hop;
    struct wvm_route_snapshot_key returned_key;
    char error[256] = {0};

    fill_rule(&flat_rules[0], WVM_ROUTE_DESTINATION_EXACT_VNODE, 0, 0, 11,
              19011, 4);
    if (finalize_snapshot(&flat, flat_rules, 1, flat_acks, 1, 7, 99, 1234, 4,
                          2, 1, error, sizeof(error)) != 0) {
        fprintf(stderr, "route-runtime setup: %s\n", error);
        return 1;
    }
    fill_rule(&successor_rules[0], WVM_ROUTE_DESTINATION_EXACT_VNODE, 0, 0,
              12, 19112, 4);
    if (finalize_snapshot(&successor, successor_rules, 1, successor_acks, 1,
                          7, 99, 1234, 4, 3, 1, error, sizeof(error)) != 0) {
        fprintf(stderr, "route-runtime successor setup: %s\n", error);
        return 1;
    }
    fill_rule(&fractal_rules[0], WVM_ROUTE_DESTINATION_EXACT_VNODE, 41, 0,
              21, 19021, 4);
    fill_rule(&fractal_rules[1], WVM_ROUTE_DESTINATION_PREFIX, 99, 0, 22,
              19022, 3);
    fractal_rules[1].next_hop_kind = WVM_ROUTE_NEXT_HOP_GATEWAY;
    fractal_rules[1].next_hop_member.role_type = WVM_MANIFEST_ROLE_GATEWAY;
    if (finalize_snapshot(&fractal, fractal_rules, 2, fractal_acks, 1, 8, 7,
                          88, 4, 1, 2, error, sizeof(error)) != 0) {
        fprintf(stderr, "route-runtime fractal setup: %s\n", error);
        return 1;
    }

    wvm_route_runtime_init(&runtime);
    if (expect(wvm_route_runtime_prepare(&runtime, &flat, error,
                                         sizeof(error)) == 0,
               "prepare flat snapshot") ||
        expect(wvm_route_runtime_activate(&runtime, &flat.route_snapshot_key,
                                          error, sizeof(error)) == 0,
               "activate flat snapshot")) {
        wvm_route_runtime_destroy(&runtime);
        return 1;
    }
    envelope_for_snapshot(&envelope, &flat.route_snapshot_key,
                          WVM_ENVELOPE_V1_ROUTE_DESTINATION_FLAT_VNODE, 0, 0);
    if (expect(wvm_route_runtime_lookup(&runtime, &envelope, &next_hop, error,
                                        sizeof(error)) == 0 &&
                   next_hop.next_hop_endpoint.data_port == 19011 &&
                   next_hop.matched_destination_kind ==
                       WVM_ROUTE_DESTINATION_EXACT_VNODE,
               "look up flat vnode zero without raw-ID fallback") ||
        expect(wvm_route_runtime_lookup_destination(
                   &runtime, &flat.route_snapshot_key,
                   WVM_ENVELOPE_V1_ROUTE_DESTINATION_FLAT_VNODE, 0, 0,
                   &next_hop, error, sizeof(error)) == 0 &&
                   next_hop.hop_limit == 4 &&
                   next_hop.next_hop_endpoint.data_port == 19011,
               "resolve flat outbound route from an immutable snapshot") ||
        expect(wvm_route_runtime_current_key(
                   &runtime, &flat.route_snapshot_key.scope_key,
                   &returned_key) == 0 &&
                   memcmp(&returned_key, &flat.route_snapshot_key,
                          sizeof(returned_key)) == 0,
               "return flat active key")) {
        wvm_route_runtime_destroy(&runtime);
        return 1;
    }

    if (expect(wvm_route_runtime_prepare(&runtime, &successor, error,
                                         sizeof(error)) == 0 &&
                   wvm_route_runtime_activate(
                       &runtime, &successor.route_snapshot_key, error,
                       sizeof(error)) == 0,
               "replace flat snapshot") ||
        expect(wvm_route_runtime_has_snapshot(&runtime,
                                              &flat.route_snapshot_key),
               "retain exact predecessor") ||
        expect(wvm_route_runtime_lookup(&runtime, &envelope, &next_hop, error,
                                        sizeof(error)) != 0,
               "reject stale packet on retained predecessor")) {
        wvm_route_runtime_destroy(&runtime);
        return 1;
    }
    envelope_for_snapshot(&envelope, &successor.route_snapshot_key,
                          WVM_ENVELOPE_V1_ROUTE_DESTINATION_FLAT_VNODE, 0, 0);
    if (expect(wvm_route_runtime_lookup(&runtime, &envelope, &next_hop, error,
                                        sizeof(error)) == 0 &&
                   next_hop.next_hop_endpoint.data_port == 19112,
               "route normal traffic through successor") ||
        expect(wvm_route_runtime_retire(&runtime, &flat.route_snapshot_key,
                                        error, sizeof(error)) == 0,
               "retire predecessor")) {
        wvm_route_runtime_destroy(&runtime);
        return 1;
    }

    if (expect(wvm_route_runtime_prepare(&runtime, &fractal, error,
                                         sizeof(error)) == 0 &&
                   wvm_route_runtime_activate(&runtime,
                                              &fractal.route_snapshot_key,
                                              error, sizeof(error)) == 0,
               "activate independent fractal scope")) {
        wvm_route_runtime_destroy(&runtime);
        return 1;
    }
    envelope_for_snapshot(&envelope, &fractal.route_snapshot_key,
                          WVM_ENVELOPE_V1_ROUTE_DESTINATION_FRACTAL_VNODE, 41,
                          0);
    if (expect(wvm_route_runtime_lookup(&runtime, &envelope, &next_hop, error,
                                        sizeof(error)) == 0 &&
                   next_hop.next_hop_endpoint.data_port == 19021 &&
                   next_hop.matched_destination_kind ==
                       WVM_ROUTE_DESTINATION_EXACT_VNODE,
               "prefer scoped exact fractal leaf route")) {
        wvm_route_runtime_destroy(&runtime);
        return 1;
    }
    envelope_for_snapshot(&envelope, &fractal.route_snapshot_key,
                          WVM_ENVELOPE_V1_ROUTE_DESTINATION_FRACTAL_VNODE, 99,
                          77);
    if (expect(wvm_route_runtime_lookup(&runtime, &envelope, &next_hop, error,
                                        sizeof(error)) == 0 &&
                   next_hop.next_hop_endpoint.data_port == 19022 &&
                   next_hop.matched_destination_kind ==
                       WVM_ROUTE_DESTINATION_PREFIX,
               "route fractal scope through prefix next hop") ||
        expect(wvm_route_runtime_lookup_destination(
                   &runtime, &fractal.route_snapshot_key,
                   WVM_ENVELOPE_V1_ROUTE_DESTINATION_FRACTAL_VNODE, 99, 77,
                   &next_hop, error, sizeof(error)) == 0 &&
                   next_hop.hop_limit == 3 &&
                   next_hop.matched_destination_kind ==
                       WVM_ROUTE_DESTINATION_PREFIX,
               "resolve fractal outbound route without flattening scope") ||
        expect((envelope.route.hop_count = next_hop.hop_limit,
                wvm_route_runtime_lookup(&runtime, &envelope, &next_hop,
                                         error, sizeof(error)) != 0),
               "enforce per-rule hop budget")) {
        wvm_route_runtime_destroy(&runtime);
        return 1;
    }

    if (expect(wvm_route_runtime_retire(&runtime, &successor.route_snapshot_key,
                                        error, sizeof(error)) == 0 &&
                   wvm_route_runtime_retire(&runtime,
                                            &fractal.route_snapshot_key,
                                            error, sizeof(error)) == 0,
               "retire independent active route scopes")) {
        wvm_route_runtime_destroy(&runtime);
        return 1;
    }
    wvm_route_runtime_destroy(&runtime);
    puts("route-runtime tests: PASS");
    return 0;
}
