#ifndef WAVEVM_NODE_RUNTIME_V1_DIRECTORY_STORE_H
#define WAVEVM_NODE_RUNTIME_V1_DIRECTORY_STORE_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "../common_include/wavevm_memory_v1.h"

#define WVM_V1_DIRECTORY_STORE_BUCKETS 4096U

struct wvm_v1_directory_store_config {
    uint32_t initial_epoch;
    uint64_t max_page_records;
};

struct wvm_v1_directory_page;

struct wvm_v1_directory_store {
    struct wvm_v1_directory_store_config config;
    struct wvm_v1_directory_page **buckets;
    pthread_mutex_t *bucket_locks;
    pthread_mutex_t allocation_lock;
    uint64_t page_record_count;
    int initialized;
};

int wvm_v1_directory_store_init(
    struct wvm_v1_directory_store *store,
    const struct wvm_v1_directory_store_config *config, char *error,
    size_t error_len);
void wvm_v1_directory_store_destroy(struct wvm_v1_directory_store *store);

/*
 * Read or lazily materialize one authoritative page. A newly materialized
 * page is zero-filled at (initial_epoch, 1). The caller receives a complete
 * page snapshot and version.
 */
int wvm_v1_directory_store_read_page(
    struct wvm_v1_directory_store *store, uint64_t gpa,
    uint8_t data[WVM_V1_MEMORY_PAGE_BYTES], uint64_t *version_out,
    char *error, size_t error_len);

/*
 * Apply one diff while holding the GPA bucket lock. The page must already be
 * materialized, the base version must match exactly, and the returned version
 * is the next version in the same epoch.
 */
int wvm_v1_directory_store_commit_page(
    struct wvm_v1_directory_store *store, uint64_t gpa,
    uint64_t base_version, uint16_t offset, const uint8_t *data,
    size_t data_bytes, uint64_t *result_version, char *error,
    size_t error_len);

uint64_t wvm_v1_directory_store_page_count(
    const struct wvm_v1_directory_store *store);

#endif /* WAVEVM_NODE_RUNTIME_V1_DIRECTORY_STORE_H */
