#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

#include "wavevm_protocol.h"
#include "wavevm_route_runtime.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "route-runtime test: %s\n", message);
        return -1;
    }
    return 0;
}

int main(void)
{
    struct wvm_route_runtime runtime;
    struct wvm_route_snapshot_key key;
    struct wvm_route_snapshot_key successor_key;
    struct wvm_route_snapshot_key third_key;
    struct wvm_route_runtime_entry entries[2];
    struct sockaddr_in next_hop;
    struct wvm_route_snapshot_key returned_key;
    char error[256] = {0};
    uint32_t target_one = WVM_ENCODE_ID(7, 11);
    uint32_t target_two = WVM_ENCODE_ID(7, 23);

    memset(&key, 0, sizeof(key));
    key.scope_key.vm_id = 7;
    key.scope_key.vm_incarnation = 99;
    key.scope_key.route_scope_id = 1234;
    key.topology_revision = 4;
    key.route_generation = 2;
    memset(key.snapshot_digest, 0x5a, sizeof(key.snapshot_digest));

    memset(entries, 0, sizeof(entries));
    entries[0].destination_id = target_one;
    entries[0].next_hop.sin_family = AF_INET;
    entries[0].next_hop.sin_addr.s_addr = inet_addr("192.0.2.11");
    entries[0].next_hop.sin_port = htons(19011);
    entries[1].destination_id = target_two;
    entries[1].next_hop.sin_family = AF_INET;
    entries[1].next_hop.sin_addr.s_addr = inet_addr("192.0.2.23");
    entries[1].next_hop.sin_port = htons(19023);

    wvm_route_runtime_init(&runtime);
    if (expect(wvm_route_runtime_prepare(&runtime, &key, entries, 2, error,
                                         sizeof(error)) == 0,
               "prepare snapshot") ||
        expect(wvm_route_runtime_activate(&runtime, &key, error,
                                          sizeof(error)) == 0,
               "activate snapshot") ||
        expect(wvm_route_runtime_lookup(&runtime, target_one, &next_hop,
                                        &returned_key) == 0,
               "lookup exact composite target") ||
        expect(next_hop.sin_port == htons(19011), "return endpoint") ||
        expect(memcmp(&returned_key, &key, sizeof(key)) == 0,
               "return snapshot key")) {
        wvm_route_runtime_destroy(&runtime);
        return 1;
    }

    successor_key = key;
    successor_key.route_generation++;
    memset(successor_key.snapshot_digest, 0x6b,
           sizeof(successor_key.snapshot_digest));
    entries[0].next_hop.sin_port = htons(19111);
    if (expect(wvm_route_runtime_prepare(&runtime, &successor_key, entries, 2,
                                         error, sizeof(error)) == 0,
               "prepare successor snapshot") ||
        expect(wvm_route_runtime_activate(&runtime, &successor_key, error,
                                          sizeof(error)) == 0,
               "activate successor snapshot") ||
        expect(wvm_route_runtime_current_key(&runtime, &returned_key) == 0 &&
                   memcmp(&returned_key, &successor_key,
                          sizeof(successor_key)) == 0,
               "successor is current") ||
        expect(wvm_route_runtime_predecessor_key(&runtime, &returned_key) == 0 &&
                   memcmp(&returned_key, &key, sizeof(key)) == 0,
               "predecessor is retained") ||
        expect(wvm_route_runtime_has_snapshot(&runtime, &key),
               "predecessor remains queryable") ||
        expect(wvm_route_runtime_lookup(&runtime, target_one, &next_hop,
                                        &returned_key) == 0 &&
                   next_hop.sin_port == htons(19111) &&
                   memcmp(&returned_key, &successor_key,
                          sizeof(successor_key)) == 0,
               "normal traffic uses successor")) {
        wvm_route_runtime_destroy(&runtime);
        return 1;
    }

    third_key = successor_key;
    third_key.route_generation++;
    memset(third_key.snapshot_digest, 0x7c, sizeof(third_key.snapshot_digest));
    if (expect(wvm_route_runtime_prepare(&runtime, &third_key, entries, 2,
                                         error, sizeof(error)) == 0,
               "prepare third snapshot") ||
        expect(wvm_route_runtime_activate(&runtime, &third_key, error,
                                          sizeof(error)) != 0,
               "reject replacement before predecessor retirement") ||
        expect(wvm_route_runtime_retire(&runtime, &key, error,
                                        sizeof(error)) == 0,
               "retire exact predecessor") ||
        expect(!wvm_route_runtime_has_snapshot(&runtime, &key),
               "retired predecessor is unavailable") ||
        expect(wvm_route_runtime_activate(&runtime, &third_key, error,
                                          sizeof(error)) == 0,
               "activate third snapshot after predecessor retirement") ||
        expect(wvm_route_runtime_predecessor_key(&runtime, &returned_key) == 0 &&
                   memcmp(&returned_key, &successor_key,
                          sizeof(successor_key)) == 0,
               "second predecessor is retained") ||
        expect(wvm_route_runtime_retire(&runtime, &successor_key, error,
                                        sizeof(error)) == 0,
               "retire second predecessor") ||
        expect(wvm_route_runtime_lookup(&runtime, 11, &next_hop, NULL) != 0,
               "reject raw fallback for nonzero VM snapshot") ||
        expect(wvm_route_runtime_retire(&runtime, &third_key, error,
                                        sizeof(error)) == 0,
               "retire active snapshot") ||
        expect(wvm_route_runtime_lookup(&runtime, target_two, &next_hop,
                                        NULL) != 0,
               "reject lookup after retirement")) {
        wvm_route_runtime_destroy(&runtime);
        return 1;
    }

    wvm_route_runtime_destroy(&runtime);
    puts("route-runtime tests: PASS");
    return 0;
}
