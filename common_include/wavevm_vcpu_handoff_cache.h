#ifndef WAVEVM_VCPU_HANDOFF_CACHE_H
#define WAVEVM_VCPU_HANDOFF_CACHE_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_vcpu_handoff.h"

enum wvm_vcpu_handoff_cache_decision {
    WVM_VCPU_HANDOFF_CACHE_EXECUTE = 1,
    WVM_VCPU_HANDOFF_CACHE_IN_PROGRESS = 2,
    WVM_VCPU_HANDOFF_CACHE_REPLAY = 3,
    WVM_VCPU_HANDOFF_CACHE_RESULT_EXPIRED = 4,
};

/*
 * Completion records are retained for REPLAY_WINDOW_MS. Their vCPU sequence
 * watermark remains for the cache lifetime, so a delayed duplicate cannot be
 * re-executed after its replay payload is reclaimed.
 */
struct wvm_vcpu_handoff_cache {
    void *entries;
    void *watermarks;
    size_t capacity;
    size_t max_result_bytes;
    uint64_t replay_window_ms;
    void *lock;
    int initialized;
};

int wvm_vcpu_handoff_cache_init(struct wvm_vcpu_handoff_cache *cache,
                                size_t capacity, uint64_t replay_window_ms,
                                size_t max_result_bytes, char *error,
                                size_t error_len);

void wvm_vcpu_handoff_cache_destroy(struct wvm_vcpu_handoff_cache *cache);

/*
 * Begin a destination-side handoff. A replay decision copies the cached typed
 * result into REPLAY_OUTPUT. IN_PROGRESS and RESULT_EXPIRED return no bytes.
 */
int wvm_vcpu_handoff_cache_begin(
    struct wvm_vcpu_handoff_cache *cache,
    const struct wvm_vcpu_handoff_request *request,
    const struct wvm_envelope *envelope, uint64_t now_ms,
    enum wvm_vcpu_handoff_cache_decision *decision, uint8_t *replay_output,
    size_t replay_capacity, size_t *replay_bytes, char *error,
    size_t error_len);

/*
 * Record one terminal typed result. Completing the same operation twice is
 * idempotent only when the encoded result is byte-for-byte identical.
 */
int wvm_vcpu_handoff_cache_complete(
    struct wvm_vcpu_handoff_cache *cache,
    const struct wvm_vcpu_handoff_request *request,
    const struct wvm_envelope *envelope,
    const struct wvm_vcpu_handoff_result *result, uint64_t now_ms,
    char *error, size_t error_len);

void wvm_vcpu_handoff_cache_prune(struct wvm_vcpu_handoff_cache *cache,
                                  uint64_t now_ms);

#endif /* WAVEVM_VCPU_HANDOFF_CACHE_H */
