#ifndef WAVEVM_IDENTITY_H
#define WAVEVM_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_sha256.h"

#define WVM_IDENTITY_ID_BYTES 16U
#define WVM_LOCAL_NAMESPACE_MAX_BYTES 128U

enum wvm_namespace_abi {
    WVM_NAMESPACE_ABI_LEGACY = 1,
    WVM_NAMESPACE_ABI_V1_U32 = 2,
};

enum wvm_vm_namespace_state {
    WVM_VM_NAMESPACE_FREE = 1,
    WVM_VM_NAMESPACE_ALLOCATED = 2,
    WVM_VM_NAMESPACE_ACTIVE = 3,
    WVM_VM_NAMESPACE_RETIRING = 4,
    WVM_VM_NAMESPACE_QUARANTINED = 5,
};

/*
 * This is coordinator-owned durable identity state.  The current allocation
 * incarnation is explicit so a retry or a delayed packet cannot revive an
 * older VM lifetime after a namespace is reused.
 */
struct wvm_vm_namespace_record {
    uint32_t vm_id;
    uint64_t next_vm_incarnation;
    uint64_t current_vm_incarnation;
    enum wvm_vm_namespace_state state;
    enum wvm_namespace_abi namespace_abi;
    uint64_t legacy_cluster_epoch;
    uint8_t retirement_record_digest[WVM_SHA256_DIGEST_BYTES];
};

struct wvm_vm_namespace_allocator {
    struct wvm_vm_namespace_record *records;
    size_t record_capacity;
    size_t record_count;
    uint32_t next_v1_vm_id;
    uint32_t next_legacy_vm_id;
    uint64_t legacy_cluster_epoch;
};

struct wvm_local_name_identity {
    uint32_t vm_id;
    uint64_t vm_incarnation;
    uint64_t manifest_generation;
    uint32_t physical_node_id;
    uint8_t manifest_id[WVM_IDENTITY_ID_BYTES];
    uint8_t admission_tx_id[WVM_IDENTITY_ID_BYTES];
};

struct wvm_local_name_namespace {
    char namespace_name[WVM_LOCAL_NAMESPACE_MAX_BYTES + 1U];
    uint8_t derivation_salt_digest[WVM_SHA256_DIGEST_BYTES];
    uint64_t name_generation;
};

void wvm_vm_namespace_allocator_init(struct wvm_vm_namespace_allocator *allocator,
                                     struct wvm_vm_namespace_record *records,
                                     size_t record_capacity,
                                     uint64_t legacy_cluster_epoch);

int wvm_vm_namespace_allocate(struct wvm_vm_namespace_allocator *allocator,
                               enum wvm_namespace_abi namespace_abi,
                               uint32_t *vm_id, uint64_t *vm_incarnation,
                               char *error, size_t error_len);

int wvm_vm_namespace_activate(struct wvm_vm_namespace_allocator *allocator,
                              uint32_t vm_id, uint64_t vm_incarnation,
                              char *error, size_t error_len);

int wvm_vm_namespace_begin_retire(struct wvm_vm_namespace_allocator *allocator,
                                  uint32_t vm_id, uint64_t vm_incarnation,
                                  char *error, size_t error_len);

int wvm_vm_namespace_quarantine(
    struct wvm_vm_namespace_allocator *allocator, uint32_t vm_id,
    uint64_t vm_incarnation,
    const uint8_t retirement_record_digest[WVM_SHA256_DIGEST_BYTES],
    int retirement_ready, char *error, size_t error_len);

int wvm_vm_namespace_release(struct wvm_vm_namespace_allocator *allocator,
                             uint32_t vm_id, uint64_t vm_incarnation,
                             char *error, size_t error_len);

/*
 * Reconstruct a namespace held by a durable transaction. Recovery never marks
 * an identity FREE; lifecycle teardown must prove retirement before reuse.
 */
int wvm_vm_namespace_restore(
    struct wvm_vm_namespace_allocator *allocator,
    enum wvm_namespace_abi namespace_abi, uint32_t vm_id,
    uint64_t vm_incarnation, enum wvm_vm_namespace_state state, char *error,
    size_t error_len);

const struct wvm_vm_namespace_record *
wvm_vm_namespace_find(const struct wvm_vm_namespace_allocator *allocator,
                      uint32_t vm_id);

int wvm_local_name_namespace_derive(
    const struct wvm_local_name_identity *identity,
    struct wvm_local_name_namespace *namespace_out, char *error,
    size_t error_len);

int wvm_local_name_namespace_validate(
    const struct wvm_local_name_namespace *namespace_value, char *error,
    size_t error_len);

int wvm_local_name_namespace_validate_unique(
    const struct wvm_local_name_namespace *namespaces, size_t namespace_count,
    char *error, size_t error_len);

#endif /* WAVEVM_IDENTITY_H */
