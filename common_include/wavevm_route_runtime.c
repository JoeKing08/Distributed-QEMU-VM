#include "wavevm_route_runtime.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wavevm_sha256.h"
#include "wavevm_protocol.h"

static void set_error(char *error, size_t error_len, const char *fmt, ...)
{
    va_list ap;

    if (!error || error_len == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(error, error_len, fmt, ap);
    va_end(ap);
}

static int route_key_equal(const struct wvm_route_snapshot_key *left,
                           const struct wvm_route_snapshot_key *right)
{
    return left && right &&
           left->scope_key.vm_id == right->scope_key.vm_id &&
           left->scope_key.vm_incarnation == right->scope_key.vm_incarnation &&
           left->scope_key.route_scope_id == right->scope_key.route_scope_id &&
           left->topology_revision == right->topology_revision &&
           left->route_generation == right->route_generation &&
           memcmp(left->snapshot_digest, right->snapshot_digest,
                  WVM_SHA256_DIGEST_BYTES) == 0;
}

static int validate_entries(const struct wvm_route_snapshot_key *key,
                            const struct wvm_route_runtime_entry *entries,
                            size_t entry_count, char *error, size_t error_len)
{
    size_t i;

    if (!key || wvm_route_snapshot_key_validate(key, error, error_len) != 0 ||
        entry_count == 0 || entry_count > WVM_ROUTE_RUNTIME_MAX_ENTRIES ||
        !entries) {
        set_error(error, error_len, "route runtime snapshot is invalid");
        return -1;
    }
    for (i = 0; i < entry_count; i++) {
        uint32_t vm_id = WVM_GET_VMID(entries[i].destination_id);

        if (entries[i].destination_id == WVM_NODE_AUTO_ROUTE ||
            entries[i].next_hop.sin_family != AF_INET ||
            entries[i].next_hop.sin_port == 0 ||
            entries[i].next_hop.sin_addr.s_addr == 0 ||
            (vm_id != 0 && vm_id != key->scope_key.vm_id)) {
            set_error(error, error_len,
                      "route entry %zu has invalid namespace or endpoint", i);
            return -1;
        }
        if (i != 0 &&
            entries[i - 1].destination_id >= entries[i].destination_id) {
            set_error(error, error_len,
                      "route entries are not strictly ordered");
            return -1;
        }
    }
    return 0;
}

static void snapshot_clear(struct wvm_route_runtime_snapshot *snapshot)
{
    if (!snapshot) {
        return;
    }
    free(snapshot->entries);
    memset(snapshot, 0, sizeof(*snapshot));
}

static void digest_entries(const struct wvm_route_runtime_entry *entries,
                           size_t entry_count,
                           uint8_t digest[WVM_SHA256_DIGEST_BYTES])
{
    struct wvm_sha256_ctx context;
    uint8_t count_bytes[8];
    size_t i;

    memset(count_bytes, 0, sizeof(count_bytes));
    for (i = 0; i < sizeof(count_bytes); i++) {
        count_bytes[sizeof(count_bytes) - 1U - i] =
            (uint8_t)(entry_count >> (i * 8U));
    }
    wvm_sha256_init(&context);
    wvm_sha256_update(&context, count_bytes, sizeof(count_bytes));
    for (i = 0; i < entry_count; i++) {
        uint8_t id_bytes[4];
        id_bytes[0] = (uint8_t)(entries[i].destination_id >> 24);
        id_bytes[1] = (uint8_t)(entries[i].destination_id >> 16);
        id_bytes[2] = (uint8_t)(entries[i].destination_id >> 8);
        id_bytes[3] = (uint8_t)entries[i].destination_id;
        wvm_sha256_update(&context, id_bytes, sizeof(id_bytes));
        wvm_sha256_update(&context, &entries[i].next_hop,
                          sizeof(entries[i].next_hop));
    }
    wvm_sha256_final(&context, digest);
}

void wvm_route_runtime_init(struct wvm_route_runtime *runtime)
{
    if (!runtime) {
        return;
    }
    memset(runtime, 0, sizeof(*runtime));
    pthread_rwlock_init(&runtime->lock, NULL);
}

void wvm_route_runtime_destroy(struct wvm_route_runtime *runtime)
{
    if (!runtime) {
        return;
    }
    pthread_rwlock_wrlock(&runtime->lock);
    snapshot_clear(&runtime->active);
    snapshot_clear(&runtime->prepared);
    snapshot_clear(&runtime->predecessor);
    runtime->has_active = 0;
    runtime->has_prepared = 0;
    runtime->has_predecessor = 0;
    pthread_rwlock_unlock(&runtime->lock);
    pthread_rwlock_destroy(&runtime->lock);
}

int wvm_route_runtime_prepare(
    struct wvm_route_runtime *runtime,
    const struct wvm_route_snapshot_key *key,
    const struct wvm_route_runtime_entry *entries, size_t entry_count,
    char *error, size_t error_len)
{
    struct wvm_route_runtime_snapshot candidate;

    if (!runtime || validate_entries(key, entries, entry_count, error,
                                     error_len) != 0) {
        return -1;
    }
    memset(&candidate, 0, sizeof(candidate));
    candidate.entries = calloc(entry_count, sizeof(*candidate.entries));
    if (!candidate.entries) {
        set_error(error, error_len, "cannot allocate route snapshot");
        return -1;
    }
    candidate.key = *key;
    memcpy(candidate.entries, entries, entry_count * sizeof(*entries));
    candidate.entry_count = entry_count;
    candidate.entry_capacity = entry_count;
    digest_entries(candidate.entries, candidate.entry_count,
                   candidate.content_digest);

    pthread_rwlock_wrlock(&runtime->lock);
    snapshot_clear(&runtime->prepared);
    runtime->prepared = candidate;
    runtime->has_prepared = 1;
    pthread_rwlock_unlock(&runtime->lock);
    return 0;
}

int wvm_route_runtime_activate(struct wvm_route_runtime *runtime,
                               const struct wvm_route_snapshot_key *key,
                               char *error, size_t error_len)
{
    if (!runtime || !key) {
        set_error(error, error_len, "route activation input is missing");
        return -1;
    }
    pthread_rwlock_wrlock(&runtime->lock);
    if (!runtime->has_prepared ||
        !route_key_equal(key, &runtime->prepared.key)) {
        pthread_rwlock_unlock(&runtime->lock);
        set_error(error, error_len, "route activation key is not prepared");
        return -1;
    }
    if (runtime->has_active &&
        route_key_equal(key, &runtime->active.key)) {
        snapshot_clear(&runtime->prepared);
        runtime->has_prepared = 0;
        pthread_rwlock_unlock(&runtime->lock);
        return 0;
    }
    if (runtime->has_predecessor) {
        pthread_rwlock_unlock(&runtime->lock);
        set_error(error, error_len,
                  "route predecessor must retire before another replacement");
        return -1;
    }
    if (runtime->has_active) {
        runtime->predecessor = runtime->active;
        runtime->has_predecessor = 1;
        memset(&runtime->active, 0, sizeof(runtime->active));
    }
    runtime->active = runtime->prepared;
    memset(&runtime->prepared, 0, sizeof(runtime->prepared));
    runtime->has_active = 1;
    runtime->has_prepared = 0;
    pthread_rwlock_unlock(&runtime->lock);
    return 0;
}

int wvm_route_runtime_retire(struct wvm_route_runtime *runtime,
                             const struct wvm_route_snapshot_key *key,
                             char *error, size_t error_len)
{
    if (!runtime || !key) {
        set_error(error, error_len, "route retirement input is missing");
        return -1;
    }
    pthread_rwlock_wrlock(&runtime->lock);
    if (runtime->has_predecessor &&
        route_key_equal(key, &runtime->predecessor.key)) {
        snapshot_clear(&runtime->predecessor);
        runtime->has_predecessor = 0;
    } else if (runtime->has_active &&
               route_key_equal(key, &runtime->active.key)) {
        snapshot_clear(&runtime->active);
        runtime->has_active = 0;
    } else {
        pthread_rwlock_unlock(&runtime->lock);
        set_error(error, error_len, "route retirement key is not retained");
        return -1;
    }
    pthread_rwlock_unlock(&runtime->lock);
    return 0;
}

int wvm_route_runtime_lookup(
    const struct wvm_route_runtime *runtime, uint32_t destination_id,
    struct sockaddr_in *next_hop_out,
    struct wvm_route_snapshot_key *key_out)
{
    size_t left;
    size_t right;
    const struct wvm_route_runtime_snapshot *snapshot;

    if (!runtime || !next_hop_out || destination_id == WVM_NODE_AUTO_ROUTE) {
        return -1;
    }
    pthread_rwlock_rdlock((pthread_rwlock_t *)&runtime->lock);
    if (!runtime->has_active) {
        pthread_rwlock_unlock((pthread_rwlock_t *)&runtime->lock);
        return -1;
    }
    snapshot = &runtime->active;
    if (WVM_GET_VMID(destination_id) != 0 &&
        WVM_GET_VMID(destination_id) != snapshot->key.scope_key.vm_id) {
        pthread_rwlock_unlock((pthread_rwlock_t *)&runtime->lock);
        return -1;
    }
    left = 0;
    right = snapshot->entry_count;
    while (left < right) {
        size_t middle = left + (right - left) / 2U;
        uint32_t current = snapshot->entries[middle].destination_id;

        if (current == destination_id) {
            *next_hop_out = snapshot->entries[middle].next_hop;
            if (key_out) {
                *key_out = snapshot->key;
            }
            pthread_rwlock_unlock((pthread_rwlock_t *)&runtime->lock);
            return 0;
        }
        if (current < destination_id) {
            left = middle + 1U;
        } else {
            right = middle;
        }
    }
    /*
     * Raw IDs are legacy-only.  A nonzero VM namespace never falls through
     * to the raw node entry, even when a compatibility route exists.
     */
    if (WVM_GET_VMID(destination_id) == 0) {
        left = 0;
        right = snapshot->entry_count;
        while (left < right) {
            size_t middle = left + (right - left) / 2U;
            uint32_t current = snapshot->entries[middle].destination_id;

            if (current == destination_id) {
                *next_hop_out = snapshot->entries[middle].next_hop;
                if (key_out) {
                    *key_out = snapshot->key;
                }
                pthread_rwlock_unlock((pthread_rwlock_t *)&runtime->lock);
                return 0;
            }
            if (current < destination_id) {
                left = middle + 1U;
            } else {
                right = middle;
            }
        }
    }
    pthread_rwlock_unlock((pthread_rwlock_t *)&runtime->lock);
    return -1;
}

int wvm_route_runtime_current_key(
    const struct wvm_route_runtime *runtime,
    struct wvm_route_snapshot_key *key_out)
{
    if (!runtime || !key_out) {
        return -1;
    }
    pthread_rwlock_rdlock((pthread_rwlock_t *)&runtime->lock);
    if (!runtime->has_active) {
        pthread_rwlock_unlock((pthread_rwlock_t *)&runtime->lock);
        return -1;
    }
    *key_out = runtime->active.key;
    pthread_rwlock_unlock((pthread_rwlock_t *)&runtime->lock);
    return 0;
}

int wvm_route_runtime_predecessor_key(
    const struct wvm_route_runtime *runtime,
    struct wvm_route_snapshot_key *key_out)
{
    if (!runtime || !key_out) {
        return -1;
    }
    pthread_rwlock_rdlock((pthread_rwlock_t *)&runtime->lock);
    if (!runtime->has_predecessor) {
        pthread_rwlock_unlock((pthread_rwlock_t *)&runtime->lock);
        return -1;
    }
    *key_out = runtime->predecessor.key;
    pthread_rwlock_unlock((pthread_rwlock_t *)&runtime->lock);
    return 0;
}

int wvm_route_runtime_has_snapshot(
    const struct wvm_route_runtime *runtime,
    const struct wvm_route_snapshot_key *key)
{
    int found;

    if (!runtime || !key) {
        return 0;
    }
    pthread_rwlock_rdlock((pthread_rwlock_t *)&runtime->lock);
    found = (runtime->has_active &&
             route_key_equal(key, &runtime->active.key)) ||
            (runtime->has_predecessor &&
             route_key_equal(key, &runtime->predecessor.key));
    pthread_rwlock_unlock((pthread_rwlock_t *)&runtime->lock);
    return found;
}
