#ifndef WAVEVM_FAULT_ENGINE_H
#define WAVEVM_FAULT_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_capability.h"
#include "wavevm_manifest.h"

enum wvm_fault_engine_id {
    WVM_FAULT_ENGINE_SIGSEGV_MPROTECT = 1,
    WVM_FAULT_ENGINE_USERFAULTFD = 2,
    WVM_FAULT_ENGINE_KVM_DIRTY_LOG = 3,
    WVM_FAULT_ENGINE_NODE_RUNTIME_RESYNC = 4,
    WVM_FAULT_ENGINE_KERNEL_ACCELERATION = 5,
};

#define WVM_FAULT_ENGINE_ROLE_DIRTY_CAPTURE (1U << 0)
#define WVM_FAULT_ENGINE_ROLE_READ_RESYNC (1U << 1)
#define WVM_FAULT_ENGINE_ROLE_INVALIDATION (1U << 2)

enum wvm_fault_range_mode {
    WVM_FAULT_RANGE_MODE_READ = 1,
    WVM_FAULT_RANGE_MODE_WRITE = 2,
    WVM_FAULT_RANGE_MODE_INVALID = 3,
};

struct wvm_fault_engine_scope {
    uint32_t vm_id;
    uint64_t vm_incarnation;
    uint64_t manifest_generation;
    uint32_t physical_node_id;
    uint64_t node_instance_id;
    uint8_t candidate_manifest_digest[WVM_SHA256_DIGEST_BYTES];
};

struct wvm_fault_range {
    uint64_t gpa_start;
    uint64_t bytes;
};

struct wvm_fault_page_key {
    uint64_t gpa;
    uint64_t required_version;
};

struct wvm_fault_dirty_journal {
    uint64_t fence_id;
    uint64_t first_gpa;
    uint64_t page_count;
    uint64_t observed_epoch;
};

struct wvm_fault_engine_ops {
    enum wvm_fault_engine_id engine_id;
    uint32_t supported_roles;
    void *provider_context;
    int (*probe)(void *provider_context,
                 const struct wvm_fault_engine_scope *scope,
                 struct wvm_capability_record *record, char *error,
                 size_t error_len);
    int (*prepare_vm)(void *provider_context,
                      const struct wvm_fault_engine_scope *scope,
                      void **engine_context, char *error, size_t error_len);
    int (*register_ram_range)(void *engine_context,
                              const struct wvm_fault_range *range, char *error,
                              size_t error_len);
    int (*arm_range)(void *engine_context, const struct wvm_fault_range *range,
                     enum wvm_fault_range_mode mode, char *error,
                     size_t error_len);
    int (*capture_dirty)(void *engine_context, uint64_t fence_id,
                         struct wvm_fault_dirty_journal *journal, char *error,
                         size_t error_len);
    int (*resolve_read_or_resync)(void *engine_context,
                                  const struct wvm_fault_page_key *page,
                                  char *error, size_t error_len);
    int (*invalidate)(void *engine_context,
                      const struct wvm_fault_page_key *page, char *error,
                      size_t error_len);
    int (*complete_fence)(void *engine_context, uint64_t fence_id,
                          int succeeded, char *error, size_t error_len);
    int (*disarm_range)(void *engine_context,
                        const struct wvm_fault_range *range, char *error,
                        size_t error_len);
    void (*teardown_vm)(void *engine_context);
};

struct wvm_fault_engine_registry {
    const struct wvm_fault_engine_ops **entries;
    size_t count;
    size_t capacity;
};

struct wvm_fault_engine_binding {
    struct wvm_fault_engine_scope scope;
    const struct wvm_fault_engine_ops *dirty_engine;
    const struct wvm_fault_engine_ops *read_engine;
    const struct wvm_fault_engine_ops *invalidation_engine;
    void *dirty_context;
    void *read_context;
    void *invalidation_context;
    int prepared;
};

int wvm_fault_engine_profile_validate(
    const struct wvm_execution_fault_profile *profile, char *error,
    size_t error_len);

int wvm_fault_engine_registry_init(struct wvm_fault_engine_registry *registry,
                                   const struct wvm_fault_engine_ops **entries,
                                   size_t capacity, char *error,
                                   size_t error_len);

int wvm_fault_engine_registry_register(
    struct wvm_fault_engine_registry *registry,
    const struct wvm_fault_engine_ops *ops, char *error, size_t error_len);

int wvm_fault_engine_binding_prepare(
    const struct wvm_fault_engine_registry *registry,
    const struct wvm_execution_fault_profile *profile,
    const struct wvm_fault_engine_scope *scope,
    struct wvm_fault_engine_binding *binding, char *error, size_t error_len);

int wvm_fault_engine_binding_register_range(
    struct wvm_fault_engine_binding *binding,
    const struct wvm_fault_range *range, char *error, size_t error_len);

int wvm_fault_engine_binding_arm_range(
    struct wvm_fault_engine_binding *binding,
    const struct wvm_fault_range *range, enum wvm_fault_range_mode mode,
    char *error, size_t error_len);

int wvm_fault_engine_binding_capture_dirty(
    struct wvm_fault_engine_binding *binding, uint64_t fence_id,
    struct wvm_fault_dirty_journal *journal, char *error, size_t error_len);

int wvm_fault_engine_binding_resolve_read_or_resync(
    struct wvm_fault_engine_binding *binding,
    const struct wvm_fault_page_key *page, char *error, size_t error_len);

int wvm_fault_engine_binding_invalidate(
    struct wvm_fault_engine_binding *binding,
    const struct wvm_fault_page_key *page, char *error, size_t error_len);

int wvm_fault_engine_binding_complete_fence(
    struct wvm_fault_engine_binding *binding, uint64_t fence_id, int succeeded,
    char *error, size_t error_len);

int wvm_fault_engine_binding_disarm_range(
    struct wvm_fault_engine_binding *binding,
    const struct wvm_fault_range *range, char *error, size_t error_len);

void wvm_fault_engine_binding_teardown(
    struct wvm_fault_engine_binding *binding);

#endif /* WAVEVM_FAULT_ENGINE_H */
