#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wavevm_route_delivery.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "route-delivery test: %s\n", message);
        return -1;
    }
    return 0;
}

static void fill_endpoint(struct wvm_endpoint *endpoint)
{
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->data_transport = WVM_DATA_TRANSPORT_UDP;
    endpoint->data_address_bytes = 4;
    endpoint->data_address[0] = 192;
    endpoint->data_address[1] = 0;
    endpoint->data_address[2] = 2;
    endpoint->data_address[3] = 17;
    endpoint->data_port = 19017;
    endpoint->control_transport = WVM_CONTROL_TRANSPORT_TLS_TCP;
    endpoint->control_port = 19117;
}

int main(void)
{
    struct wvm_route_snapshot_key key;
    struct wvm_route_rule_record rule;
    struct wvm_required_ack_entry ack;
    struct wvm_route_snapshot_record snapshot;
    struct wvm_route_snapshot_file_storage loaded;
    uint8_t encoded[8192];
    uint8_t digest[WVM_SHA256_DIGEST_BYTES];
    size_t encoded_bytes;
    char path[] = "/tmp/wavevm-route-delivery-XXXXXX";
    char route_path[256];
    char error[256] = {0};
    int fd;

    memset(&key, 0, sizeof(key));
    key.scope_key.vm_id = 77;
    key.scope_key.vm_incarnation = 9;
    key.scope_key.route_scope_id = 12;
    key.topology_revision = 4;
    key.route_generation = 3;

    memset(&rule, 0, sizeof(rule));
    rule.destination_kind = WVM_ROUTE_DESTINATION_EXACT_VNODE;
    rule.destination_scope = 0;
    rule.destination_vnode_or_endpoint = 17;
    rule.next_hop_kind = WVM_ROUTE_NEXT_HOP_ENDPOINT;
    rule.next_hop_member.role_type = WVM_MANIFEST_ROLE_NODE_RUNTIME;
    rule.next_hop_member.role_id = 17;
    rule.next_hop_member.instance_id = 91;
    fill_endpoint(&rule.next_hop_endpoint);
    rule.hop_limit = 8;

    memset(&ack, 0, sizeof(ack));
    ack.member_key = rule.next_hop_member;
    ack.role_type = WVM_MANIFEST_ROLE_NODE_RUNTIME;
    ack.expected_snapshot_key = key;
    ack.endpoint = rule.next_hop_endpoint;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.route_snapshot_key = key;
    snapshot.membership_revision = 4;
    snapshot.topology_kind = 1;
    snapshot.next_hop_rules.entries = &rule;
    snapshot.next_hop_rules.count = 1;
    snapshot.next_hop_rules.capacity = 1;
    snapshot.required_ack_set.entries.entries = &ack;
    snapshot.required_ack_set.entries.count = 1;
    snapshot.required_ack_set.entries.capacity = 1;
    snapshot.operation_retention_horizon_ms = 5000;
    snapshot.retirement_policy = 1;

    if (expect(wvm_route_snapshot_record_encode(
                   &snapshot, encoded, sizeof(encoded), &encoded_bytes, digest,
                   error, sizeof(error)) == 0,
               "derive route snapshot digest")) {
        unlink(path);
        return 1;
    }
    memcpy(snapshot.route_snapshot_key.snapshot_digest, digest, sizeof(digest));
    memcpy(ack.expected_snapshot_key.snapshot_digest, digest, sizeof(digest));

    fd = mkstemp(path);
    if (fd < 0) {
        perror("mkstemp");
        return 1;
    }
    close(fd);
    unlink(path);

    if (expect(wvm_route_snapshot_path_from_manifest(
                   path, route_path, sizeof(route_path), error,
                   sizeof(error)) == 0,
               "derive manifest route path") ||
        expect(strlen(route_path) > 6 &&
                   strcmp(route_path + strlen(route_path) - 6, ".route") == 0,
               "derive deterministic route suffix") ||
        expect(wvm_route_snapshot_file_publish(route_path, &snapshot, error,
                                               sizeof(error)) == 0,
               "publish route snapshot")) {
        unlink(route_path);
        return 1;
    }

    wvm_route_snapshot_file_storage_init(&loaded);
    if (expect(wvm_route_snapshot_file_load(route_path, &loaded, error,
                                            sizeof(error)) == 0,
               "load published route snapshot") ||
        expect(wvm_route_snapshot_file_matches(
                   &loaded, &snapshot.route_snapshot_key, error,
                   sizeof(error)) == 0,
               "match admitted route key") ||
        expect(loaded.snapshot.next_hop_rules.count == 1,
               "preserve route rule") ||
        expect(wvm_route_snapshot_file_matches(
                   &loaded,
                   &(struct wvm_route_snapshot_key){
                       .scope_key = {.vm_id = 78,
                                     .vm_incarnation = 9,
                                     .route_scope_id = 12},
                       .topology_revision = 4,
                       .route_generation = 3},
                   error, sizeof(error)) != 0,
               "reject a different VM scope")) {
        wvm_route_snapshot_file_storage_free(&loaded);
        unlink(route_path);
        return 1;
    }

    wvm_route_snapshot_file_storage_free(&loaded);
    unlink(route_path);
    puts("route-delivery tests: PASS");
    return 0;
}
