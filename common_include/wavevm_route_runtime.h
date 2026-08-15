#ifndef WAVEVM_ROUTE_RUNTIME_H
#define WAVEVM_ROUTE_RUNTIME_H

/*
 * User-space runtime view of one immutable route snapshot.
 *
 * The control plane owns the snapshot.  Data-plane consumers only publish a
 * prepared copy and perform exact-key lookups.  This deliberately does not
 * implement discovery or route learning.
 */

#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "wavevm_manifest.h"

#define WVM_ROUTE_RUNTIME_MAX_ENTRIES 65536U

struct wvm_route_runtime_entry {
    uint32_t destination_id;
    struct sockaddr_in next_hop;
};

struct wvm_route_runtime_snapshot {
    struct wvm_route_snapshot_key key;
    struct wvm_route_runtime_entry *entries;
    size_t entry_count;
    size_t entry_capacity;
    uint8_t content_digest[WVM_SHA256_DIGEST_BYTES];
};

struct wvm_route_runtime {
    pthread_rwlock_t lock;
    struct wvm_route_runtime_snapshot active;
    struct wvm_route_runtime_snapshot prepared;
    struct wvm_route_runtime_snapshot predecessor;
    int has_active;
    int has_prepared;
    int has_predecessor;
};

void wvm_route_runtime_init(struct wvm_route_runtime *runtime);
void wvm_route_runtime_destroy(struct wvm_route_runtime *runtime);

/*
 * Prepare an immutable copy.  The caller retains ownership of entries after
 * this call returns.  A prepared snapshot is not usable for data traffic
 * until activate() succeeds.
 */
int wvm_route_runtime_prepare(
    struct wvm_route_runtime *runtime,
    const struct wvm_route_snapshot_key *key,
    const struct wvm_route_runtime_entry *entries, size_t entry_count,
    char *error, size_t error_len);

int wvm_route_runtime_activate(struct wvm_route_runtime *runtime,
                               const struct wvm_route_snapshot_key *key,
                               char *error, size_t error_len);

/*
 * Retire one exact active or retained predecessor snapshot. A route miss is
 * returned to the caller; callers must not retry by stripping a nonzero VM
 * namespace.
 */
int wvm_route_runtime_retire(struct wvm_route_runtime *runtime,
                             const struct wvm_route_snapshot_key *key,
                             char *error, size_t error_len);

int wvm_route_runtime_lookup(
    const struct wvm_route_runtime *runtime, uint32_t destination_id,
    struct sockaddr_in *next_hop_out,
    struct wvm_route_snapshot_key *key_out);

int wvm_route_runtime_current_key(
    const struct wvm_route_runtime *runtime,
    struct wvm_route_snapshot_key *key_out);

/*
 * A successor activation retains one exact predecessor for operation/query
 * drain. New data traffic always uses the active generation; callers must
 * explicitly retire the predecessor after its completion horizon.
 */
int wvm_route_runtime_predecessor_key(
    const struct wvm_route_runtime *runtime,
    struct wvm_route_snapshot_key *key_out);
int wvm_route_runtime_has_snapshot(
    const struct wvm_route_runtime *runtime,
    const struct wvm_route_snapshot_key *key);

#endif
