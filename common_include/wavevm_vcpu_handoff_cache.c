#include "wavevm_vcpu_handoff_cache.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum cache_entry_state {
    CACHE_ENTRY_IN_PROGRESS = 1,
    CACHE_ENTRY_COMPLETE = 2,
};

struct cache_entry {
    int used;
    enum cache_entry_state state;
    uint32_t vm_id;
    uint64_t vm_incarnation;
    uint32_t origin_physical_node_id;
    uint64_t origin_runtime_instance_id;
    uint32_t vcpu_index;
    uint64_t handoff_sequence;
    uint8_t operation_id[16];
    uint8_t semantic_payload_digest[WVM_SHA256_DIGEST_BYTES];
    uint8_t *result;
    size_t result_bytes;
    uint64_t result_expires_at_ms;
};

struct sequence_watermark {
    int used;
    uint32_t vm_id;
    uint64_t vm_incarnation;
    uint32_t origin_physical_node_id;
    uint64_t origin_runtime_instance_id;
    uint32_t vcpu_index;
    uint64_t completed_sequence;
};

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

static struct cache_entry *entries(const struct wvm_vcpu_handoff_cache *cache)
{
    return (struct cache_entry *)cache->entries;
}

static struct sequence_watermark *watermarks(
    const struct wvm_vcpu_handoff_cache *cache)
{
    return (struct sequence_watermark *)cache->watermarks;
}

static pthread_mutex_t *cache_lock(const struct wvm_vcpu_handoff_cache *cache)
{
    return (pthread_mutex_t *)cache->lock;
}

static int same_lane_request(const struct cache_entry *entry,
                             const struct wvm_vcpu_handoff_request *request)
{
    return entry->vm_id == request->vm_id &&
           entry->vm_incarnation == request->vm_incarnation &&
           entry->origin_physical_node_id ==
               request->origin_physical_node_id &&
           entry->origin_runtime_instance_id ==
               request->origin_runtime_instance_id &&
           entry->vcpu_index == request->vcpu_index;
}

static int same_operation_request(
    const struct cache_entry *entry,
    const struct wvm_vcpu_handoff_request *request)
{
    return same_lane_request(entry, request) &&
           entry->handoff_sequence == request->handoff_sequence &&
           memcmp(entry->operation_id, request->operation_id,
                  sizeof(entry->operation_id)) == 0;
}

static int same_sequence_request(
    const struct cache_entry *entry,
    const struct wvm_vcpu_handoff_request *request)
{
    return same_lane_request(entry, request) &&
           entry->handoff_sequence == request->handoff_sequence;
}

static int same_watermark_request(
    const struct sequence_watermark *watermark,
    const struct wvm_vcpu_handoff_request *request)
{
    return watermark->vm_id == request->vm_id &&
           watermark->vm_incarnation == request->vm_incarnation &&
           watermark->origin_physical_node_id ==
               request->origin_physical_node_id &&
           watermark->origin_runtime_instance_id ==
               request->origin_runtime_instance_id &&
           watermark->vcpu_index == request->vcpu_index;
}

static uint64_t expiry_after(uint64_t now_ms, uint64_t duration_ms)
{
    if (UINT64_MAX - now_ms < duration_ms) {
        return UINT64_MAX;
    }
    return now_ms + duration_ms;
}

static int validate_request_envelope(
    const struct wvm_vcpu_handoff_request *request,
    const struct wvm_envelope *envelope,
    uint8_t semantic_payload_digest[WVM_SHA256_DIGEST_BYTES], char *error,
    size_t error_len)
{
    if (wvm_vcpu_handoff_request_validate_envelope(request, envelope, error,
                                                    error_len) != 0) {
        return -EINVAL;
    }
    wvm_envelope_semantic_digest(envelope->payload, envelope->payload_bytes,
                                 semantic_payload_digest);
    if (memcmp(semantic_payload_digest, envelope->semantic_payload_digest,
               WVM_SHA256_DIGEST_BYTES) != 0) {
        set_error(error, error_len,
                  "vCPU handoff envelope semantic digest mismatch");
        return -EPROTO;
    }
    return 0;
}

static void prune_locked(struct wvm_vcpu_handoff_cache *cache,
                         uint64_t now_ms)
{
    struct cache_entry *entry_array = entries(cache);
    size_t i;

    for (i = 0; i < cache->capacity; i++) {
        struct cache_entry *entry = &entry_array[i];

        if (entry->used && entry->state == CACHE_ENTRY_COMPLETE &&
            entry->result_expires_at_ms <= now_ms) {
            free(entry->result);
            memset(entry, 0, sizeof(*entry));
        }
    }
}

int wvm_vcpu_handoff_cache_init(struct wvm_vcpu_handoff_cache *cache,
                                size_t capacity, uint64_t replay_window_ms,
                                size_t max_result_bytes, char *error,
                                size_t error_len)
{
    pthread_mutex_t *lock;
    int lock_result;

    if (!cache || capacity == 0 || replay_window_ms == 0 ||
        max_result_bytes < WVM_VCPU_HANDOFF_RESULT_HEADER_BYTES ||
        max_result_bytes >
            WVM_ENVELOPE_MAX_NETWORK_LOGICAL_PAYLOAD) {
        set_error(error, error_len, "vCPU handoff cache configuration is invalid");
        return -EINVAL;
    }
    memset(cache, 0, sizeof(*cache));
    cache->entries = calloc(capacity, sizeof(struct cache_entry));
    cache->watermarks = calloc(capacity, sizeof(struct sequence_watermark));
    lock = calloc(1, sizeof(*lock));
    if (!cache->entries || !cache->watermarks || !lock) {
        free(cache->entries);
        free(cache->watermarks);
        free(lock);
        memset(cache, 0, sizeof(*cache));
        set_error(error, error_len, "cannot allocate vCPU handoff cache");
        return -ENOMEM;
    }
    lock_result = pthread_mutex_init(lock, NULL);
    if (lock_result != 0) {
        free(cache->entries);
        free(cache->watermarks);
        free(lock);
        memset(cache, 0, sizeof(*cache));
        set_error(error, error_len, "cannot initialize vCPU handoff cache");
        return -lock_result;
    }
    cache->capacity = capacity;
    cache->max_result_bytes = max_result_bytes;
    cache->replay_window_ms = replay_window_ms;
    cache->lock = lock;
    cache->initialized = 1;
    return 0;
}

void wvm_vcpu_handoff_cache_destroy(struct wvm_vcpu_handoff_cache *cache)
{
    struct cache_entry *entry_array;
    pthread_mutex_t *lock;
    size_t i;

    if (!cache) {
        return;
    }
    entry_array = entries(cache);
    lock = cache_lock(cache);
    if (entry_array) {
        for (i = 0; i < cache->capacity; i++) {
            free(entry_array[i].result);
        }
    }
    if (lock) {
        pthread_mutex_destroy(lock);
    }
    free(entry_array);
    free(watermarks(cache));
    free(lock);
    memset(cache, 0, sizeof(*cache));
}

void wvm_vcpu_handoff_cache_prune(struct wvm_vcpu_handoff_cache *cache,
                                  uint64_t now_ms)
{
    if (!cache || !cache->initialized || !cache_lock(cache)) {
        return;
    }
    pthread_mutex_lock(cache_lock(cache));
    prune_locked(cache, now_ms);
    pthread_mutex_unlock(cache_lock(cache));
}

int wvm_vcpu_handoff_cache_begin(
    struct wvm_vcpu_handoff_cache *cache,
    const struct wvm_vcpu_handoff_request *request,
    const struct wvm_envelope *envelope, uint64_t now_ms,
    enum wvm_vcpu_handoff_cache_decision *decision, uint8_t *replay_output,
    size_t replay_capacity, size_t *replay_bytes, char *error,
    size_t error_len)
{
    struct cache_entry *entry_array;
    struct sequence_watermark *watermark_array;
    struct sequence_watermark *watermark = NULL;
    struct cache_entry *free_entry = NULL;
    uint8_t semantic_payload_digest[WVM_SHA256_DIGEST_BYTES];
    size_t i;
    int validation;

    if (!cache || !cache->initialized || !request || !envelope || !decision ||
        !replay_bytes || !cache_lock(cache)) {
        set_error(error, error_len, "vCPU handoff cache input is invalid");
        return -EINVAL;
    }
    validation = validate_request_envelope(request, envelope,
                                           semantic_payload_digest, error,
                                           error_len);
    if (validation != 0) {
        return validation;
    }

    *replay_bytes = 0;
    entry_array = entries(cache);
    watermark_array = watermarks(cache);
    pthread_mutex_lock(cache_lock(cache));
    prune_locked(cache, now_ms);
    for (i = 0; i < cache->capacity; i++) {
        struct cache_entry *entry = &entry_array[i];

        if (!entry->used) {
            if (!free_entry) {
                free_entry = entry;
            }
            continue;
        }
        if (same_operation_request(entry, request)) {
            if (memcmp(entry->semantic_payload_digest, semantic_payload_digest,
                       sizeof(semantic_payload_digest)) != 0) {
                pthread_mutex_unlock(cache_lock(cache));
                set_error(error, error_len,
                          "vCPU handoff reuses operation ID with another payload");
                return -EPROTO;
            }
            if (entry->state == CACHE_ENTRY_IN_PROGRESS) {
                *decision = WVM_VCPU_HANDOFF_CACHE_IN_PROGRESS;
                pthread_mutex_unlock(cache_lock(cache));
                return 0;
            }
            if (!replay_output || replay_capacity < entry->result_bytes) {
                pthread_mutex_unlock(cache_lock(cache));
                set_error(error, error_len,
                          "vCPU handoff replay output is too small");
                return -ENOBUFS;
            }
            memcpy(replay_output, entry->result, entry->result_bytes);
            *replay_bytes = entry->result_bytes;
            *decision = WVM_VCPU_HANDOFF_CACHE_REPLAY;
            pthread_mutex_unlock(cache_lock(cache));
            return 0;
        }
        if (same_sequence_request(entry, request)) {
            pthread_mutex_unlock(cache_lock(cache));
            set_error(error, error_len,
                      "vCPU handoff sequence has another operation ID");
            return -EPROTO;
        }
        if (entry->state == CACHE_ENTRY_IN_PROGRESS &&
            same_lane_request(entry, request)) {
            pthread_mutex_unlock(cache_lock(cache));
            set_error(error, error_len,
                      "vCPU has an earlier handoff still in progress");
            return -EALREADY;
        }
    }
    for (i = 0; i < cache->capacity; i++) {
        if (watermark_array[i].used &&
            same_watermark_request(&watermark_array[i], request)) {
            watermark = &watermark_array[i];
            break;
        }
    }
    if (watermark) {
        if (request->handoff_sequence <= watermark->completed_sequence) {
            *decision = WVM_VCPU_HANDOFF_CACHE_RESULT_EXPIRED;
            pthread_mutex_unlock(cache_lock(cache));
            return 0;
        }
        if (request->handoff_sequence !=
            watermark->completed_sequence + 1U) {
            pthread_mutex_unlock(cache_lock(cache));
            set_error(error, error_len,
                      "vCPU handoff sequence skips an uncompleted interval");
            return -EPROTO;
        }
    } else {
        if (!free_entry) {
            pthread_mutex_unlock(cache_lock(cache));
            set_error(error, error_len,
                      "vCPU handoff completion cache is full");
            return -ENOSPC;
        }
        for (i = 0; i < cache->capacity; i++) {
            if (!watermark_array[i].used) {
                watermark = &watermark_array[i];
                memset(watermark, 0, sizeof(*watermark));
                watermark->used = 1;
                watermark->vm_id = request->vm_id;
                watermark->vm_incarnation = request->vm_incarnation;
                watermark->origin_physical_node_id =
                    request->origin_physical_node_id;
                watermark->origin_runtime_instance_id =
                    request->origin_runtime_instance_id;
                watermark->vcpu_index = request->vcpu_index;
                watermark->completed_sequence = request->handoff_sequence - 1U;
                break;
            }
        }
        if (!watermark) {
            pthread_mutex_unlock(cache_lock(cache));
            set_error(error, error_len, "vCPU handoff watermark cache is full");
            return -ENOSPC;
        }
    }
    if (!free_entry) {
        pthread_mutex_unlock(cache_lock(cache));
        set_error(error, error_len, "vCPU handoff completion cache is full");
        return -ENOSPC;
    }

    memset(free_entry, 0, sizeof(*free_entry));
    free_entry->used = 1;
    free_entry->state = CACHE_ENTRY_IN_PROGRESS;
    free_entry->vm_id = request->vm_id;
    free_entry->vm_incarnation = request->vm_incarnation;
    free_entry->origin_physical_node_id = request->origin_physical_node_id;
    free_entry->origin_runtime_instance_id = request->origin_runtime_instance_id;
    free_entry->vcpu_index = request->vcpu_index;
    free_entry->handoff_sequence = request->handoff_sequence;
    memcpy(free_entry->operation_id, request->operation_id,
           sizeof(free_entry->operation_id));
    memcpy(free_entry->semantic_payload_digest, semantic_payload_digest,
           sizeof(free_entry->semantic_payload_digest));
    *decision = WVM_VCPU_HANDOFF_CACHE_EXECUTE;
    pthread_mutex_unlock(cache_lock(cache));
    return 0;
}

int wvm_vcpu_handoff_cache_complete(
    struct wvm_vcpu_handoff_cache *cache,
    const struct wvm_vcpu_handoff_request *request,
    const struct wvm_envelope *envelope,
    const struct wvm_vcpu_handoff_result *result, uint64_t now_ms,
    char *error, size_t error_len)
{
    struct cache_entry *entry_array;
    struct sequence_watermark *watermark_array;
    uint8_t semantic_payload_digest[WVM_SHA256_DIGEST_BYTES];
    uint8_t *encoded_result;
    size_t encoded_bytes = 0;
    size_t i;
    int validation;

    if (!cache || !cache->initialized || !request || !envelope || !result ||
        !cache_lock(cache)) {
        set_error(error, error_len, "vCPU handoff completion input is invalid");
        return -EINVAL;
    }
    validation = validate_request_envelope(request, envelope,
                                           semantic_payload_digest, error,
                                           error_len);
    if (validation != 0 ||
        wvm_vcpu_handoff_result_validate_request(request, result, error,
                                                 error_len) != 0) {
        return validation != 0 ? validation : -EINVAL;
    }
    encoded_result = malloc(cache->max_result_bytes);
    if (!encoded_result) {
        set_error(error, error_len, "cannot allocate vCPU handoff result");
        return -ENOMEM;
    }
    if (wvm_vcpu_handoff_result_encode(
            result, encoded_result, cache->max_result_bytes, &encoded_bytes,
            error, error_len) != 0) {
        free(encoded_result);
        return -EINVAL;
    }

    entry_array = entries(cache);
    watermark_array = watermarks(cache);
    pthread_mutex_lock(cache_lock(cache));
    for (i = 0; i < cache->capacity; i++) {
        struct cache_entry *entry = &entry_array[i];

        if (!entry->used || !same_operation_request(entry, request)) {
            continue;
        }
        if (memcmp(entry->semantic_payload_digest, semantic_payload_digest,
                   sizeof(semantic_payload_digest)) != 0) {
            pthread_mutex_unlock(cache_lock(cache));
            free(encoded_result);
            set_error(error, error_len,
                      "vCPU handoff completion changes request payload");
            return -EPROTO;
        }
        if (entry->state == CACHE_ENTRY_COMPLETE) {
            int same_result = entry->result_bytes == encoded_bytes &&
                              memcmp(entry->result, encoded_result,
                                     encoded_bytes) == 0;

            pthread_mutex_unlock(cache_lock(cache));
            free(encoded_result);
            if (!same_result) {
                set_error(error, error_len,
                          "vCPU handoff has conflicting completions");
                return -EPROTO;
            }
            return 0;
        }
        entry->result = encoded_result;
        entry->result_bytes = encoded_bytes;
        entry->result_expires_at_ms =
            expiry_after(now_ms, cache->replay_window_ms);
        entry->state = CACHE_ENTRY_COMPLETE;
        for (i = 0; i < cache->capacity; i++) {
            if (watermark_array[i].used &&
                same_watermark_request(&watermark_array[i], request)) {
                watermark_array[i].completed_sequence =
                    request->handoff_sequence;
                pthread_mutex_unlock(cache_lock(cache));
                return 0;
            }
        }
        pthread_mutex_unlock(cache_lock(cache));
        free(entry->result);
        memset(entry, 0, sizeof(*entry));
        set_error(error, error_len, "vCPU handoff has no sequence watermark");
        return -EPROTO;
    }
    pthread_mutex_unlock(cache_lock(cache));
    free(encoded_result);
    set_error(error, error_len, "vCPU handoff completion is not in progress");
    return -ENOENT;
}
