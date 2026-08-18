#include <stdio.h>
#include <string.h>

#include "wavevm_fault_engine.h"

struct fake_engine {
    unsigned int probes;
    unsigned int prepares;
    unsigned int registers;
    unsigned int arms;
    unsigned int captures;
    unsigned int resolves;
    unsigned int invalidates;
    unsigned int completions;
    unsigned int disarms;
    unsigned int teardowns;
};

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "fault-engine test: %s\n", message);
        return -1;
    }
    return 0;
}

static int fake_probe(void *opaque, const struct wvm_fault_engine_scope *scope,
                      struct wvm_capability_record *record, char *error,
                      size_t error_len)
{
    struct fake_engine *engine = opaque;

    (void)record;
    (void)error;
    (void)error_len;
    if (!engine || !scope) {
        return -1;
    }
    engine->probes++;
    return 0;
}

static int fake_prepare(void *opaque, const struct wvm_fault_engine_scope *scope,
                        void **context, char *error, size_t error_len)
{
    struct fake_engine *engine = opaque;

    (void)error;
    (void)error_len;
    if (!engine || !scope || !context) {
        return -1;
    }
    engine->prepares++;
    *context = engine;
    return 0;
}

static int fake_register(void *opaque, const struct wvm_fault_range *range,
                         char *error, size_t error_len)
{
    struct fake_engine *engine = opaque;

    (void)error;
    (void)error_len;
    if (!engine || !range) {
        return -1;
    }
    engine->registers++;
    return 0;
}

static int fake_arm(void *opaque, const struct wvm_fault_range *range,
                    enum wvm_fault_range_mode mode, char *error,
                    size_t error_len)
{
    struct fake_engine *engine = opaque;

    (void)error;
    (void)error_len;
    if (!engine || !range || mode == 0) {
        return -1;
    }
    engine->arms++;
    return 0;
}

static int fake_capture(void *opaque, uint64_t fence_id,
                        struct wvm_fault_dirty_journal *journal, char *error,
                        size_t error_len)
{
    struct fake_engine *engine = opaque;

    (void)error;
    (void)error_len;
    if (!engine || !journal || fence_id == 0) {
        return -1;
    }
    engine->captures++;
    memset(journal, 0, sizeof(*journal));
    journal->fence_id = fence_id;
    journal->page_count = 1;
    return 0;
}

static int fake_resolve(void *opaque, const struct wvm_fault_page_key *page,
                        char *error, size_t error_len)
{
    struct fake_engine *engine = opaque;

    (void)error;
    (void)error_len;
    if (!engine || !page) {
        return -1;
    }
    engine->resolves++;
    return 0;
}

static int fake_invalidate(void *opaque, const struct wvm_fault_page_key *page,
                           char *error, size_t error_len)
{
    struct fake_engine *engine = opaque;

    (void)error;
    (void)error_len;
    if (!engine || !page) {
        return -1;
    }
    engine->invalidates++;
    return 0;
}

static int fake_complete(void *opaque, uint64_t fence_id, int succeeded,
                         char *error, size_t error_len)
{
    struct fake_engine *engine = opaque;

    (void)error;
    (void)error_len;
    if (!engine || fence_id == 0 || (succeeded != 0 && succeeded != 1)) {
        return -1;
    }
    engine->completions++;
    return 0;
}

static int fake_disarm(void *opaque, const struct wvm_fault_range *range,
                       char *error, size_t error_len)
{
    struct fake_engine *engine = opaque;

    (void)error;
    (void)error_len;
    if (!engine || !range) {
        return -1;
    }
    engine->disarms++;
    return 0;
}

static void fake_teardown(void *opaque)
{
    struct fake_engine *engine = opaque;

    if (engine) {
        engine->teardowns++;
    }
}

static void fill_ops(struct wvm_fault_engine_ops *ops,
                     enum wvm_fault_engine_id id, uint32_t roles,
                     struct fake_engine *engine)
{
    memset(ops, 0, sizeof(*ops));
    ops->engine_id = id;
    ops->supported_roles = roles;
    ops->provider_context = engine;
    ops->probe = fake_probe;
    ops->prepare_vm = fake_prepare;
    ops->register_ram_range = fake_register;
    ops->arm_range = fake_arm;
    ops->capture_dirty = fake_capture;
    ops->resolve_read_or_resync = fake_resolve;
    ops->invalidate = fake_invalidate;
    ops->complete_fence = fake_complete;
    ops->disarm_range = fake_disarm;
    ops->teardown_vm = fake_teardown;
}

static void fill_profile(struct wvm_execution_fault_profile *profile,
                         struct wvm_capability_ref *capability,
                         enum wvm_manifest_backend backend, uint16_t dirty,
                         uint16_t read, uint16_t invalidate)
{
    memset(profile, 0, sizeof(*profile));
    memset(capability, 0, sizeof(*capability));
    capability->physical_node_id = 17;
    capability->node_instance_id = 101;
    capability->profile_generation = 1;
    memset(capability->profile_digest, 0x41,
           sizeof(capability->profile_digest));
    profile->backend = backend;
    profile->context_schema_version = 1;
    profile->dirty_capture_engine = dirty;
    profile->read_fault_engine = read;
    profile->invalidation_engine = invalidate;
    profile->per_node_capabilities.entries = capability;
    profile->per_node_capabilities.count = 1;
    profile->per_node_capabilities.capacity = 1;
    memset(profile->supported_memory_policies_digest, 0x61,
           sizeof(profile->supported_memory_policies_digest));
    profile->fallback_decision = 1;
}

int main(void)
{
    const struct wvm_fault_engine_ops *registry_entries[3];
    struct wvm_fault_engine_registry registry;
    struct fake_engine signal_state;
    struct fake_engine kvm_state;
    struct fake_engine resync_state;
    struct wvm_fault_engine_ops signal_ops;
    struct wvm_fault_engine_ops kvm_ops;
    struct wvm_fault_engine_ops resync_ops;
    struct wvm_execution_fault_profile profile;
    struct wvm_capability_ref capability;
    struct wvm_fault_engine_scope scope;
    struct wvm_fault_engine_binding binding;
    struct wvm_fault_range range = {
        .gpa_start = 0,
        .bytes = 2 * WVM_MANIFEST_PAGE_BYTES,
    };
    struct wvm_fault_page_key page = {
        .gpa = WVM_MANIFEST_PAGE_BYTES,
        .required_version = 7,
    };
    struct wvm_fault_dirty_journal journal;
    char error[256] = {0};

    memset(&signal_state, 0, sizeof(signal_state));
    memset(&kvm_state, 0, sizeof(kvm_state));
    memset(&resync_state, 0, sizeof(resync_state));
    fill_ops(&signal_ops, WVM_FAULT_ENGINE_SIGSEGV_MPROTECT,
             WVM_FAULT_ENGINE_ROLE_DIRTY_CAPTURE |
                 WVM_FAULT_ENGINE_ROLE_READ_RESYNC |
                 WVM_FAULT_ENGINE_ROLE_INVALIDATION,
             &signal_state);
    fill_ops(&kvm_ops, WVM_FAULT_ENGINE_KVM_DIRTY_LOG,
             WVM_FAULT_ENGINE_ROLE_DIRTY_CAPTURE, &kvm_state);
    fill_ops(&resync_ops, WVM_FAULT_ENGINE_NODE_RUNTIME_RESYNC,
             WVM_FAULT_ENGINE_ROLE_READ_RESYNC |
                 WVM_FAULT_ENGINE_ROLE_INVALIDATION,
             &resync_state);
    if (expect(wvm_fault_engine_registry_init(&registry, registry_entries, 3,
                                              error, sizeof(error)) == 0,
               "initialize registry") ||
        expect(wvm_fault_engine_registry_register(&registry, &signal_ops, error,
                                                  sizeof(error)) == 0 &&
                   wvm_fault_engine_registry_register(&registry, &kvm_ops, error,
                                                      sizeof(error)) == 0 &&
                   wvm_fault_engine_registry_register(
                       &registry, &resync_ops, error, sizeof(error)) == 0,
               "register engine adapters") ||
        expect(wvm_fault_engine_registry_register(&registry, &signal_ops, error,
                                                  sizeof(error)) != 0,
               "reject duplicate engine adapter")) {
        return 1;
    }

    memset(&scope, 0, sizeof(scope));
    scope.vm_id = 256;
    scope.vm_incarnation = 1;
    scope.manifest_generation = 1;
    scope.physical_node_id = 17;
    scope.node_instance_id = 101;
    memset(scope.candidate_manifest_digest, 0x91,
           sizeof(scope.candidate_manifest_digest));
    fill_profile(&profile, &capability, WVM_MANIFEST_BACKEND_TCG,
                 WVM_FAULT_ENGINE_SIGSEGV_MPROTECT,
                 WVM_FAULT_ENGINE_SIGSEGV_MPROTECT,
                 WVM_FAULT_ENGINE_SIGSEGV_MPROTECT);
    memset(&binding, 0, sizeof(binding));
    if (expect(wvm_fault_engine_binding_prepare(&registry, &profile, &scope,
                                                &binding, error, sizeof(error)) ==
                   0,
               "prepare one manifest-bound TCG engine") ||
        expect(wvm_fault_engine_binding_register_range(&binding, &range, error,
                                                       sizeof(error)) == 0 &&
                   wvm_fault_engine_binding_arm_range(
                       &binding, &range, WVM_FAULT_RANGE_MODE_READ, error,
                       sizeof(error)) == 0 &&
                   wvm_fault_engine_binding_capture_dirty(
                       &binding, 3, &journal, error, sizeof(error)) == 0 &&
                   wvm_fault_engine_binding_resolve_read_or_resync(
                       &binding, &page, error, sizeof(error)) == 0 &&
                   wvm_fault_engine_binding_invalidate(
                       &binding, &page, error, sizeof(error)) == 0 &&
                   wvm_fault_engine_binding_complete_fence(
                       &binding, 3, 1, error, sizeof(error)) == 0 &&
                   wvm_fault_engine_binding_disarm_range(
                       &binding, &range, error, sizeof(error)) == 0,
               "run the common fault contract") ||
        expect(signal_state.prepares == 1 && signal_state.registers == 1 &&
                   signal_state.arms == 1 && signal_state.captures == 1 &&
                   signal_state.resolves == 1 && signal_state.invalidates == 1 &&
                   signal_state.completions == 1 && signal_state.disarms == 1,
               "deduplicate one engine across all roles")) {
        wvm_fault_engine_binding_teardown(&binding);
        return 1;
    }
    wvm_fault_engine_binding_teardown(&binding);
    if (expect(signal_state.teardowns == 1, "tear down TCG engine once")) {
        return 1;
    }

    fill_profile(&profile, &capability, WVM_MANIFEST_BACKEND_KVM,
                 WVM_FAULT_ENGINE_KVM_DIRTY_LOG,
                 WVM_FAULT_ENGINE_NODE_RUNTIME_RESYNC,
                 WVM_FAULT_ENGINE_NODE_RUNTIME_RESYNC);
    if (expect(wvm_fault_engine_binding_prepare(&registry, &profile, &scope,
                                                &binding, error, sizeof(error)) ==
                   0,
               "prepare KVM dirty and explicit resync engines") ||
        expect(kvm_state.prepares == 1 && resync_state.prepares == 1,
               "prepare one context for each KVM role adapter")) {
        wvm_fault_engine_binding_teardown(&binding);
        return 1;
    }
    wvm_fault_engine_binding_teardown(&binding);
    profile.read_fault_engine = WVM_FAULT_ENGINE_SIGSEGV_MPROTECT;
    if (expect(wvm_fault_engine_profile_validate(&profile, error,
                                                 sizeof(error)) != 0,
               "reject a host-protection read trap for KVM")) {
        return 1;
    }

    puts("fault-engine tests: PASS");
    return 0;
}
