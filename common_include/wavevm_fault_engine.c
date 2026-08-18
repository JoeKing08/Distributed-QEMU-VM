#include "wavevm_fault_engine.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

static int digest_is_zero(const uint8_t digest[WVM_SHA256_DIGEST_BYTES])
{
    size_t i;

    for (i = 0; i < WVM_SHA256_DIGEST_BYTES; i++) {
        if (digest[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static int engine_id_valid(uint16_t engine_id)
{
    return engine_id >= WVM_FAULT_ENGINE_SIGSEGV_MPROTECT &&
           engine_id <= WVM_FAULT_ENGINE_KERNEL_ACCELERATION;
}

static int engine_allowed(uint16_t engine_id, uint32_t allowed)
{
    return engine_id_valid(engine_id) && (allowed & (1U << engine_id)) != 0;
}

static int scope_valid(const struct wvm_fault_engine_scope *scope)
{
    return scope && scope->vm_id != 0 && scope->vm_incarnation != 0 &&
           scope->manifest_generation != 0 && scope->physical_node_id != 0 &&
           scope->node_instance_id != 0 &&
           !digest_is_zero(scope->candidate_manifest_digest);
}

static int range_valid(const struct wvm_fault_range *range)
{
    return range && range->bytes != 0 &&
           range->gpa_start % WVM_MANIFEST_PAGE_BYTES == 0 &&
           range->bytes % WVM_MANIFEST_PAGE_BYTES == 0 &&
           range->bytes <= UINT64_MAX - range->gpa_start;
}

static int page_valid(const struct wvm_fault_page_key *page)
{
    return page && page->required_version != 0 &&
           page->gpa % WVM_MANIFEST_PAGE_BYTES == 0;
}

static int ops_valid(const struct wvm_fault_engine_ops *ops, char *error,
                     size_t error_len)
{
    if (!ops || !engine_id_valid(ops->engine_id) || ops->supported_roles == 0 ||
        (ops->supported_roles & ~(WVM_FAULT_ENGINE_ROLE_DIRTY_CAPTURE |
                                  WVM_FAULT_ENGINE_ROLE_READ_RESYNC |
                                  WVM_FAULT_ENGINE_ROLE_INVALIDATION)) != 0 ||
        !ops->probe || !ops->prepare_vm || !ops->register_ram_range ||
        !ops->arm_range || !ops->capture_dirty ||
        !ops->resolve_read_or_resync || !ops->invalidate ||
        !ops->complete_fence || !ops->disarm_range || !ops->teardown_vm) {
        set_error(error, error_len, "fault engine operations are invalid");
        return -1;
    }
    return 0;
}

int wvm_fault_engine_profile_validate(
    const struct wvm_execution_fault_profile *profile, char *error,
    size_t error_len)
{
    const uint32_t tcg_capture =
        (1U << WVM_FAULT_ENGINE_SIGSEGV_MPROTECT) |
        (1U << WVM_FAULT_ENGINE_USERFAULTFD) |
        (1U << WVM_FAULT_ENGINE_KERNEL_ACCELERATION);
    const uint32_t tcg_read = (1U << WVM_FAULT_ENGINE_SIGSEGV_MPROTECT) |
                              (1U << WVM_FAULT_ENGINE_USERFAULTFD);
    const uint32_t tcg_invalidate = tcg_capture;
    const uint32_t kvm_capture = (1U << WVM_FAULT_ENGINE_KVM_DIRTY_LOG) |
                                 (1U << WVM_FAULT_ENGINE_KERNEL_ACCELERATION);
    const uint32_t kvm_read = 1U << WVM_FAULT_ENGINE_NODE_RUNTIME_RESYNC;
    const uint32_t kvm_invalidate =
        (1U << WVM_FAULT_ENGINE_NODE_RUNTIME_RESYNC) |
        (1U << WVM_FAULT_ENGINE_KERNEL_ACCELERATION);
    int uses_kernel;

    if (!profile || wvm_execution_fault_profile_validate(profile, error,
                                                         error_len) != 0) {
        set_error(error, error_len, "execution fault profile is invalid");
        return -1;
    }
    uses_kernel = profile->dirty_capture_engine ==
                      WVM_FAULT_ENGINE_KERNEL_ACCELERATION ||
                  profile->invalidation_engine ==
                      WVM_FAULT_ENGINE_KERNEL_ACCELERATION;
    if ((profile->backend == WVM_MANIFEST_BACKEND_TCG &&
         (!engine_allowed(profile->dirty_capture_engine, tcg_capture) ||
          !engine_allowed(profile->read_fault_engine, tcg_read) ||
          !engine_allowed(profile->invalidation_engine, tcg_invalidate))) ||
        (profile->backend == WVM_MANIFEST_BACKEND_KVM &&
         (!engine_allowed(profile->dirty_capture_engine, kvm_capture) ||
          !engine_allowed(profile->read_fault_engine, kvm_read) ||
          !engine_allowed(profile->invalidation_engine, kvm_invalidate))) ||
        (uses_kernel && profile->kernel_accelerator_bits == 0) ||
        (!uses_kernel && profile->kernel_accelerator_bits != 0 &&
         profile->dirty_capture_engine != WVM_FAULT_ENGINE_KERNEL_ACCELERATION &&
         profile->invalidation_engine != WVM_FAULT_ENGINE_KERNEL_ACCELERATION)) {
        set_error(error, error_len,
                  "execution fault profile selects an incompatible engine");
        return -1;
    }
    return 0;
}

int wvm_fault_engine_registry_init(struct wvm_fault_engine_registry *registry,
                                   const struct wvm_fault_engine_ops **entries,
                                   size_t capacity, char *error,
                                   size_t error_len)
{
    if (!registry || !entries || capacity == 0) {
        set_error(error, error_len, "fault engine registry storage is invalid");
        return -1;
    }
    memset(registry, 0, sizeof(*registry));
    registry->entries = entries;
    registry->capacity = capacity;
    memset(entries, 0, capacity * sizeof(*entries));
    return 0;
}

int wvm_fault_engine_registry_register(
    struct wvm_fault_engine_registry *registry,
    const struct wvm_fault_engine_ops *ops, char *error, size_t error_len)
{
    size_t i;

    if (!registry || !registry->entries ||
        ops_valid(ops, error, error_len) != 0) {
        return -1;
    }
    for (i = 0; i < registry->count; i++) {
        if (registry->entries[i]->engine_id == ops->engine_id) {
            set_error(error, error_len, "fault engine ID is already registered");
            return -1;
        }
    }
    if (registry->count == registry->capacity) {
        set_error(error, error_len, "fault engine registry is full");
        return -1;
    }
    registry->entries[registry->count++] = ops;
    return 0;
}

static const struct wvm_fault_engine_ops *find_engine(
    const struct wvm_fault_engine_registry *registry, uint16_t engine_id,
    uint32_t required_role, char *error, size_t error_len)
{
    size_t i;

    if (!registry || !registry->entries) {
        set_error(error, error_len, "fault engine registry is invalid");
        return NULL;
    }
    for (i = 0; i < registry->count; i++) {
        const struct wvm_fault_engine_ops *ops = registry->entries[i];

        if (ops_valid(ops, error, error_len) != 0) {
            return NULL;
        }
        if (ops->engine_id == engine_id) {
            if ((ops->supported_roles & required_role) == 0) {
                set_error(error, error_len,
                          "fault engine lacks its selected role");
                return NULL;
            }
            return ops;
        }
    }
    set_error(error, error_len, "selected fault engine is not registered");
    return NULL;
}

static void binding_clear(struct wvm_fault_engine_binding *binding)
{
    if (binding) {
        memset(binding, 0, sizeof(*binding));
    }
}

static int prepare_engine(const struct wvm_fault_engine_ops *ops,
                          const struct wvm_fault_engine_scope *scope,
                          void **context, char *error, size_t error_len)
{
    if (!ops || !scope || !context ||
        ops->prepare_vm(ops->provider_context, scope, context, error,
                        error_len) != 0 ||
        !*context) {
        if (!error || error[0] == '\0') {
            set_error(error, error_len, "fault engine preparation failed");
        }
        return -1;
    }
    return 0;
}

int wvm_fault_engine_binding_prepare(
    const struct wvm_fault_engine_registry *registry,
    const struct wvm_execution_fault_profile *profile,
    const struct wvm_fault_engine_scope *scope,
    struct wvm_fault_engine_binding *binding, char *error, size_t error_len)
{
    const struct wvm_fault_engine_ops *dirty;
    const struct wvm_fault_engine_ops *read;
    const struct wvm_fault_engine_ops *invalidation;
    void *dirty_context = NULL;
    void *read_context = NULL;
    void *invalidation_context = NULL;

    if (!binding || !scope_valid(scope) ||
        wvm_fault_engine_profile_validate(profile, error, error_len) != 0) {
        set_error(error, error_len, "fault engine binding input is invalid");
        return -1;
    }
    dirty = find_engine(registry, profile->dirty_capture_engine,
                        WVM_FAULT_ENGINE_ROLE_DIRTY_CAPTURE, error, error_len);
    read = find_engine(registry, profile->read_fault_engine,
                       WVM_FAULT_ENGINE_ROLE_READ_RESYNC, error, error_len);
    invalidation = find_engine(registry, profile->invalidation_engine,
                               WVM_FAULT_ENGINE_ROLE_INVALIDATION, error,
                               error_len);
    if (!dirty || !read || !invalidation ||
        prepare_engine(dirty, scope, &dirty_context, error, error_len) != 0) {
        return -1;
    }
    if (read == dirty) {
        read_context = dirty_context;
    } else if (prepare_engine(read, scope, &read_context, error, error_len) !=
               0) {
        dirty->teardown_vm(dirty_context);
        return -1;
    }
    if (invalidation == dirty) {
        invalidation_context = dirty_context;
    } else if (invalidation == read) {
        invalidation_context = read_context;
    } else if (prepare_engine(invalidation, scope, &invalidation_context, error,
                              error_len) != 0) {
        if (read != dirty) {
            read->teardown_vm(read_context);
        }
        dirty->teardown_vm(dirty_context);
        return -1;
    }
    binding_clear(binding);
    binding->scope = *scope;
    binding->dirty_engine = dirty;
    binding->read_engine = read;
    binding->invalidation_engine = invalidation;
    binding->dirty_context = dirty_context;
    binding->read_context = read_context;
    binding->invalidation_context = invalidation_context;
    binding->prepared = 1;
    return 0;
}

static int binding_valid(const struct wvm_fault_engine_binding *binding)
{
    return binding && binding->prepared && scope_valid(&binding->scope) &&
           binding->dirty_engine && binding->read_engine &&
           binding->invalidation_engine && binding->dirty_context &&
           binding->read_context && binding->invalidation_context;
}

enum range_operation {
    RANGE_OPERATION_REGISTER = 1,
    RANGE_OPERATION_DISARM = 2,
};

static int call_range_engine(const struct wvm_fault_engine_ops *ops,
                             void *engine_context,
                             const struct wvm_fault_range *range,
                             enum range_operation operation, char *error,
                             size_t error_len)
{
    if (operation == RANGE_OPERATION_REGISTER) {
        return ops->register_ram_range(engine_context, range, error, error_len);
    }
    return ops->disarm_range(engine_context, range, error, error_len);
}

static int call_unique_range_engines(struct wvm_fault_engine_binding *binding,
                                     const struct wvm_fault_range *range,
                                     enum range_operation operation,
                                     char *error, size_t error_len)
{
    if (!binding_valid(binding) || !range_valid(range) ||
        (operation != RANGE_OPERATION_REGISTER &&
         operation != RANGE_OPERATION_DISARM)) {
        set_error(error, error_len, "fault engine range operation is invalid");
        return -1;
    }
    if (call_range_engine(binding->dirty_engine, binding->dirty_context, range,
                          operation, error, error_len) != 0 ||
        (binding->read_engine != binding->dirty_engine &&
         call_range_engine(binding->read_engine, binding->read_context, range,
                           operation, error, error_len) != 0) ||
        (binding->invalidation_engine != binding->dirty_engine &&
         binding->invalidation_engine != binding->read_engine &&
         call_range_engine(binding->invalidation_engine,
                           binding->invalidation_context, range, operation,
                           error, error_len) != 0)) {
        return -1;
    }
    return 0;
}

int wvm_fault_engine_binding_register_range(
    struct wvm_fault_engine_binding *binding,
    const struct wvm_fault_range *range, char *error, size_t error_len)
{
    return call_unique_range_engines(binding, range, RANGE_OPERATION_REGISTER,
                                     error, error_len);
}

int wvm_fault_engine_binding_arm_range(
    struct wvm_fault_engine_binding *binding,
    const struct wvm_fault_range *range, enum wvm_fault_range_mode mode,
    char *error, size_t error_len)
{
    if (!binding_valid(binding) || !range_valid(range) ||
        mode < WVM_FAULT_RANGE_MODE_READ || mode > WVM_FAULT_RANGE_MODE_INVALID) {
        set_error(error, error_len, "fault engine arm operation is invalid");
        return -1;
    }
    if (binding->dirty_engine->arm_range(binding->dirty_context, range, mode,
                                         error, error_len) != 0 ||
        (binding->read_engine != binding->dirty_engine &&
         binding->read_engine->arm_range(binding->read_context, range, mode,
                                         error, error_len) != 0) ||
        (binding->invalidation_engine != binding->dirty_engine &&
         binding->invalidation_engine != binding->read_engine &&
         binding->invalidation_engine->arm_range(binding->invalidation_context,
                                                 range, mode, error,
                                                 error_len) != 0)) {
        return -1;
    }
    return 0;
}

int wvm_fault_engine_binding_capture_dirty(
    struct wvm_fault_engine_binding *binding, uint64_t fence_id,
    struct wvm_fault_dirty_journal *journal, char *error, size_t error_len)
{
    if (!binding_valid(binding) || fence_id == 0 || !journal ||
        binding->dirty_engine->capture_dirty(binding->dirty_context, fence_id,
                                             journal, error, error_len) != 0 ||
        journal->fence_id != fence_id) {
        if (!error || error[0] == '\0') {
            set_error(error, error_len, "fault engine dirty capture failed");
        }
        return -1;
    }
    return 0;
}

int wvm_fault_engine_binding_resolve_read_or_resync(
    struct wvm_fault_engine_binding *binding,
    const struct wvm_fault_page_key *page, char *error, size_t error_len)
{
    if (!binding_valid(binding) || !page_valid(page) ||
        binding->read_engine->resolve_read_or_resync(binding->read_context, page,
                                                      error, error_len) != 0) {
        if (!error || error[0] == '\0') {
            set_error(error, error_len, "fault engine read resolution failed");
        }
        return -1;
    }
    return 0;
}

int wvm_fault_engine_binding_invalidate(
    struct wvm_fault_engine_binding *binding,
    const struct wvm_fault_page_key *page, char *error, size_t error_len)
{
    if (!binding_valid(binding) || !page_valid(page) ||
        binding->invalidation_engine->invalidate(
            binding->invalidation_context, page, error, error_len) != 0) {
        if (!error || error[0] == '\0') {
            set_error(error, error_len, "fault engine invalidation failed");
        }
        return -1;
    }
    return 0;
}

int wvm_fault_engine_binding_complete_fence(
    struct wvm_fault_engine_binding *binding, uint64_t fence_id, int succeeded,
    char *error, size_t error_len)
{
    if (!binding_valid(binding) || fence_id == 0 ||
        (succeeded != 0 && succeeded != 1) ||
        binding->dirty_engine->complete_fence(binding->dirty_context, fence_id,
                                              succeeded, error, error_len) != 0 ||
        (binding->read_engine != binding->dirty_engine &&
         binding->read_engine->complete_fence(binding->read_context, fence_id,
                                              succeeded, error, error_len) != 0) ||
        (binding->invalidation_engine != binding->dirty_engine &&
         binding->invalidation_engine != binding->read_engine &&
         binding->invalidation_engine->complete_fence(
             binding->invalidation_context, fence_id, succeeded, error,
             error_len) != 0)) {
        if (!error || error[0] == '\0') {
            set_error(error, error_len, "fault engine fence completion failed");
        }
        return -1;
    }
    return 0;
}

int wvm_fault_engine_binding_disarm_range(
    struct wvm_fault_engine_binding *binding,
    const struct wvm_fault_range *range, char *error, size_t error_len)
{
    return call_unique_range_engines(binding, range, RANGE_OPERATION_DISARM,
                                     error, error_len);
}

void wvm_fault_engine_binding_teardown(
    struct wvm_fault_engine_binding *binding)
{
    if (!binding || !binding->prepared) {
        return;
    }
    if (binding->invalidation_engine != binding->dirty_engine &&
        binding->invalidation_engine != binding->read_engine) {
        binding->invalidation_engine->teardown_vm(binding->invalidation_context);
    }
    if (binding->read_engine != binding->dirty_engine) {
        binding->read_engine->teardown_vm(binding->read_context);
    }
    binding->dirty_engine->teardown_vm(binding->dirty_context);
    binding_clear(binding);
}
