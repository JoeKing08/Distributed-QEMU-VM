#include <stdio.h>
#include <string.h>

#include "wavevm_control.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "control-route-record test: %s\n", message);
        return -1;
    }
    return 0;
}

static void fill_endpoint(struct wvm_endpoint *endpoint, uint8_t last_octet,
                          uint16_t data_port, uint16_t control_port)
{
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->data_transport = WVM_DATA_TRANSPORT_UDP;
    endpoint->data_address_bytes = 4;
    endpoint->data_address[0] = 10;
    endpoint->data_address[3] = last_octet;
    endpoint->data_port = data_port;
    endpoint->control_transport = WVM_CONTROL_TRANSPORT_TLS_TCP;
    endpoint->control_port = control_port;
}

int main(void)
{
    struct wvm_route_snapshot_key key;
    struct wvm_required_ack_entry ack_entries[1];
    struct wvm_route_rule_record rules[1];
    struct wvm_route_snapshot_record snapshot;
    struct wvm_required_ack_entry decoded_ack_entries[1];
    struct wvm_route_rule_record decoded_rules[1];
    struct wvm_route_snapshot_record decoded_snapshot;
    uint8_t bytes[8192];
    uint8_t second_bytes[8192];
    uint8_t digest[WVM_SHA256_DIGEST_BYTES];
    uint8_t second_digest[WVM_SHA256_DIGEST_BYTES];
    size_t encoded_bytes;
    size_t second_encoded_bytes;
    char error[256] = {0};

    memset(&key, 0, sizeof(key));
    key.scope_key.vm_id = 256;
    key.scope_key.vm_incarnation = 1;
    key.scope_key.route_scope_id = 1;
    key.topology_revision = 7;
    key.route_generation = 1;

    memset(ack_entries, 0, sizeof(ack_entries));
    ack_entries[0].member_key.role_type = WVM_MANIFEST_ROLE_NODE_RUNTIME;
    ack_entries[0].member_key.role_id = 17;
    ack_entries[0].member_key.instance_id = 1001;
    ack_entries[0].role_type = WVM_MANIFEST_ROLE_NODE_RUNTIME;
    ack_entries[0].expected_snapshot_key = key;
    fill_endpoint(&ack_entries[0].endpoint, 17, 9000, 9001);

    memset(rules, 0, sizeof(rules));
    rules[0].destination_kind = 1;
    rules[0].destination_vnode_or_endpoint = 17;
    rules[0].next_hop_kind = 1;
    rules[0].next_hop_member = ack_entries[0].member_key;
    rules[0].next_hop_endpoint = ack_entries[0].endpoint;
    rules[0].hop_limit = 8;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.route_snapshot_key = key;
    snapshot.membership_revision = 7;
    snapshot.topology_kind = 1;
    snapshot.next_hop_rules.entries = rules;
    snapshot.next_hop_rules.count = 1;
    snapshot.next_hop_rules.capacity = 1;
    snapshot.required_ack_set.entries.entries = ack_entries;
    snapshot.required_ack_set.entries.count = 1;
    snapshot.required_ack_set.entries.capacity = 1;
    snapshot.operation_retention_horizon_ms = 5000;
    snapshot.retirement_policy = 1;

    if (expect(wvm_route_snapshot_record_encode(
                   &snapshot, bytes, sizeof(bytes), &encoded_bytes, digest,
                   error, sizeof(error)) == 0,
               "encode self-referential route snapshot")) {
        return 1;
    }
    memcpy(snapshot.route_snapshot_key.snapshot_digest, digest, sizeof(digest));
    memcpy(ack_entries[0].expected_snapshot_key.snapshot_digest, digest,
           sizeof(digest));
    if (expect(wvm_route_snapshot_record_encode(
                   &snapshot, second_bytes, sizeof(second_bytes),
                   &second_encoded_bytes, second_digest, error,
                   sizeof(error)) == 0,
               "re-encode route snapshot") ||
        expect(encoded_bytes == second_encoded_bytes &&
                   memcmp(bytes, second_bytes, encoded_bytes) == 0,
               "route snapshot encoding is deterministic") ||
        expect(memcmp(digest, second_digest, sizeof(digest)) == 0,
               "route snapshot digest is deterministic")) {
        return 1;
    }

    memset(&decoded_snapshot, 0, sizeof(decoded_snapshot));
    decoded_snapshot.next_hop_rules.entries = decoded_rules;
    decoded_snapshot.next_hop_rules.capacity = 1;
    decoded_snapshot.required_ack_set.entries.entries = decoded_ack_entries;
    decoded_snapshot.required_ack_set.entries.capacity = 1;
    if (expect(wvm_route_snapshot_record_decode(
                   bytes, encoded_bytes, &decoded_snapshot, error,
                   sizeof(error)) == 0,
               "decode route snapshot") ||
        expect(decoded_snapshot.required_ack_set.entries.count == 1,
               "decode ACK entry") ||
        expect(memcmp(decoded_snapshot.route_snapshot_key.snapshot_digest,
                      decoded_snapshot.required_ack_set.entries.entries[0]
                          .expected_snapshot_key.snapshot_digest,
                      sizeof(digest)) == 0,
               "ACK entry binds final snapshot digest")) {
        return 1;
    }

    bytes[encoded_bytes - 1] ^= 0x01;
    if (expect(wvm_route_snapshot_record_decode(
                   bytes, encoded_bytes, &decoded_snapshot, error,
                   sizeof(error)) != 0,
               "reject route snapshot corruption")) {
        return 1;
    }

    /*
     * Route record shapes must match V1 routed-frame semantics before a
     * snapshot reaches a gateway.  These failures previously survived record
     * encoding and only appeared as runtime route misses.
     */
    rules[0].destination_kind = WVM_ROUTE_DESTINATION_PREFIX;
    rules[0].destination_scope = 9;
    rules[0].destination_vnode_or_endpoint = 1;
    rules[0].next_hop_kind = WVM_ROUTE_NEXT_HOP_GATEWAY;
    rules[0].next_hop_member.role_type = WVM_MANIFEST_ROLE_GATEWAY;
    if (expect(wvm_route_snapshot_record_validate(&snapshot, error,
                                                  sizeof(error)) != 0,
               "reject prefix with a leaf vnode")) {
        return 1;
    }
    rules[0].destination_vnode_or_endpoint = 0;
    if (expect(wvm_route_snapshot_record_validate(&snapshot, error,
                                                  sizeof(error)) != 0,
               "reject prefix from a flat snapshot")) {
        return 1;
    }
    snapshot.topology_kind = 2;
    rules[0].next_hop_member.role_type = WVM_MANIFEST_ROLE_NODE_RUNTIME;
    if (expect(wvm_route_snapshot_record_validate(&snapshot, error,
                                                  sizeof(error)) != 0,
               "reject prefix next hop without gateway role")) {
        return 1;
    }

    puts("control-route-record tests: PASS");
    return 0;
}
