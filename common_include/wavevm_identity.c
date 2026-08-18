#include "wavevm_identity.h"

#include <inttypes.h>
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

static int valid_namespace_abi(enum wvm_namespace_abi namespace_abi)
{
    return namespace_abi == WVM_NAMESPACE_ABI_LEGACY ||
           namespace_abi == WVM_NAMESPACE_ABI_U32;
}

static int valid_namespace_state(enum wvm_vm_namespace_state state)
{
    return state >= WVM_VM_NAMESPACE_FREE &&
           state <= WVM_VM_NAMESPACE_QUARANTINED;
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

static int bytes_are_zero(const uint8_t *bytes, size_t byte_count)
{
    size_t i;

    for (i = 0; i < byte_count; i++) {
        if (bytes[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static void write_be32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static void write_be64(uint8_t *dst, uint64_t value)
{
    dst[0] = (uint8_t)(value >> 56);
    dst[1] = (uint8_t)(value >> 48);
    dst[2] = (uint8_t)(value >> 40);
    dst[3] = (uint8_t)(value >> 32);
    dst[4] = (uint8_t)(value >> 24);
    dst[5] = (uint8_t)(value >> 16);
    dst[6] = (uint8_t)(value >> 8);
    dst[7] = (uint8_t)value;
}

static struct wvm_vm_namespace_record *
find_mutable(struct wvm_vm_namespace_allocator *allocator, uint32_t vm_id)
{
    size_t i;

    if (!allocator || vm_id == 0) {
        return NULL;
    }
    for (i = 0; i < allocator->record_count; i++) {
        if (allocator->records[i].vm_id == vm_id) {
            return &allocator->records[i];
        }
    }
    return NULL;
}

const struct wvm_vm_namespace_record *
wvm_vm_namespace_find(const struct wvm_vm_namespace_allocator *allocator,
                      uint32_t vm_id)
{
    size_t i;

    if (!allocator || vm_id == 0) {
        return NULL;
    }
    for (i = 0; i < allocator->record_count; i++) {
        if (allocator->records[i].vm_id == vm_id) {
            return &allocator->records[i];
        }
    }
    return NULL;
}

void wvm_vm_namespace_allocator_init(struct wvm_vm_namespace_allocator *allocator,
                                     struct wvm_vm_namespace_record *records,
                                     size_t record_capacity,
                                     uint64_t legacy_cluster_epoch)
{
    if (!allocator) {
        return;
    }

    memset(allocator, 0, sizeof(*allocator));
    allocator->records = records;
    allocator->record_capacity = record_capacity;
    allocator->next_vm_id = 256;
    allocator->next_legacy_vm_id = 1;
    allocator->legacy_cluster_epoch = legacy_cluster_epoch;
    if (records && record_capacity != 0) {
        memset(records, 0, record_capacity * sizeof(*records));
    }
}

static int record_available_for(const struct wvm_vm_namespace_allocator *allocator,
                                const struct wvm_vm_namespace_record *record,
                                enum wvm_namespace_abi namespace_abi)
{
    if (record->state != WVM_VM_NAMESPACE_FREE ||
        record->namespace_abi != namespace_abi) {
        return 0;
    }
    if (namespace_abi != WVM_NAMESPACE_ABI_LEGACY) {
        return 1;
    }
    return record->legacy_cluster_epoch != allocator->legacy_cluster_epoch;
}

static struct wvm_vm_namespace_record *
find_reusable_record(struct wvm_vm_namespace_allocator *allocator,
                     enum wvm_namespace_abi namespace_abi)
{
    size_t i;

    for (i = 0; i < allocator->record_count; i++) {
        if (record_available_for(allocator, &allocator->records[i],
                                 namespace_abi)) {
            return &allocator->records[i];
        }
    }
    return NULL;
}

static int allocate_fresh_id(struct wvm_vm_namespace_allocator *allocator,
                             enum wvm_namespace_abi namespace_abi,
                             uint32_t *vm_id)
{
    uint32_t candidate;
    uint64_t attempts;

    if (namespace_abi == WVM_NAMESPACE_ABI_LEGACY) {
        for (attempts = 0; attempts < 255; attempts++) {
            candidate = allocator->next_legacy_vm_id;
            allocator->next_legacy_vm_id =
                candidate == 255 ? 1 : candidate + 1;
            if (!wvm_vm_namespace_find(allocator, candidate)) {
                *vm_id = candidate;
                return 0;
            }
        }
        return -1;
    }

    for (attempts = 0; attempts < UINT32_MAX; attempts++) {
        candidate = allocator->next_vm_id;
        allocator->next_vm_id =
            candidate == UINT32_MAX ? 256 : candidate + 1;
        if (candidate < 256) {
            continue;
        }
        if (!wvm_vm_namespace_find(allocator, candidate)) {
            *vm_id = candidate;
            return 0;
        }
    }
    return -1;
}

int wvm_vm_namespace_allocate(struct wvm_vm_namespace_allocator *allocator,
                               enum wvm_namespace_abi namespace_abi,
                               uint32_t *vm_id, uint64_t *vm_incarnation,
                               char *error, size_t error_len)
{
    struct wvm_vm_namespace_record *record;
    uint32_t allocated_id;

    if (!allocator || !allocator->records || allocator->record_capacity == 0 ||
        !vm_id || !vm_incarnation || !valid_namespace_abi(namespace_abi)) {
        set_error(error, error_len, "invalid namespace allocation request");
        return -1;
    }

    record = find_reusable_record(allocator, namespace_abi);
    if (record) {
        allocated_id = record->vm_id;
    } else {
        if (allocator->record_count == allocator->record_capacity ||
            allocate_fresh_id(allocator, namespace_abi, &allocated_id) != 0) {
            set_error(error, error_len, "no reusable VM namespace is available");
            return -1;
        }
        record = &allocator->records[allocator->record_count++];
        memset(record, 0, sizeof(*record));
        record->vm_id = allocated_id;
        record->next_vm_incarnation = 1;
        record->state = WVM_VM_NAMESPACE_FREE;
    }

    if (record->vm_id == 0 || record->next_vm_incarnation == 0 ||
        record->next_vm_incarnation == UINT64_MAX) {
        set_error(error, error_len, "VM namespace %u has exhausted incarnations",
                  record->vm_id);
        return -1;
    }

    record->current_vm_incarnation = record->next_vm_incarnation++;
    record->namespace_abi = namespace_abi;
    record->legacy_cluster_epoch =
        namespace_abi == WVM_NAMESPACE_ABI_LEGACY
            ? allocator->legacy_cluster_epoch
            : 0;
    record->state = WVM_VM_NAMESPACE_ALLOCATED;
    memset(record->retirement_record_digest, 0,
           sizeof(record->retirement_record_digest));
    *vm_id = record->vm_id;
    *vm_incarnation = record->current_vm_incarnation;
    return 0;
}

static int validate_transition(struct wvm_vm_namespace_allocator *allocator,
                               uint32_t vm_id, uint64_t vm_incarnation,
                               enum wvm_vm_namespace_state expected,
                               struct wvm_vm_namespace_record **record_out,
                               char *error, size_t error_len)
{
    struct wvm_vm_namespace_record *record = find_mutable(allocator, vm_id);

    if (!record || !valid_namespace_state(record->state) ||
        record->state != expected ||
        record->current_vm_incarnation != vm_incarnation ||
        vm_incarnation == 0) {
        set_error(error, error_len,
                  "VM namespace %u is not in the expected lifecycle state",
                  vm_id);
        return -1;
    }
    *record_out = record;
    return 0;
}

int wvm_vm_namespace_activate(struct wvm_vm_namespace_allocator *allocator,
                              uint32_t vm_id, uint64_t vm_incarnation,
                              char *error, size_t error_len)
{
    struct wvm_vm_namespace_record *record;

    if (validate_transition(allocator, vm_id, vm_incarnation,
                            WVM_VM_NAMESPACE_ALLOCATED, &record, error,
                            error_len) != 0) {
        return -1;
    }
    record->state = WVM_VM_NAMESPACE_ACTIVE;
    return 0;
}

int wvm_vm_namespace_begin_retire(struct wvm_vm_namespace_allocator *allocator,
                                  uint32_t vm_id, uint64_t vm_incarnation,
                                  char *error, size_t error_len)
{
    struct wvm_vm_namespace_record *record = find_mutable(allocator, vm_id);

    if (!record || record->current_vm_incarnation != vm_incarnation ||
        (record->state != WVM_VM_NAMESPACE_ALLOCATED &&
         record->state != WVM_VM_NAMESPACE_ACTIVE)) {
        set_error(error, error_len,
                  "VM namespace %u cannot begin retirement", vm_id);
        return -1;
    }
    record->state = WVM_VM_NAMESPACE_RETIRING;
    return 0;
}

int wvm_vm_namespace_quarantine(
    struct wvm_vm_namespace_allocator *allocator, uint32_t vm_id,
    uint64_t vm_incarnation,
    const uint8_t retirement_record_digest[WVM_SHA256_DIGEST_BYTES],
    int retirement_ready, char *error, size_t error_len)
{
    struct wvm_vm_namespace_record *record;

    if (!retirement_record_digest || !retirement_ready ||
        digest_is_zero(retirement_record_digest) ||
        validate_transition(allocator, vm_id, vm_incarnation,
                            WVM_VM_NAMESPACE_RETIRING, &record, error,
                            error_len) != 0) {
        if (retirement_ready == 0) {
            set_error(error, error_len,
                      "VM namespace %u has not completed retirement checks",
                      vm_id);
        }
        return -1;
    }
    memcpy(record->retirement_record_digest, retirement_record_digest,
           sizeof(record->retirement_record_digest));
    record->state = WVM_VM_NAMESPACE_QUARANTINED;
    return 0;
}

int wvm_vm_namespace_release(struct wvm_vm_namespace_allocator *allocator,
                             uint32_t vm_id, uint64_t vm_incarnation,
                             char *error, size_t error_len)
{
    struct wvm_vm_namespace_record *record;

    if (validate_transition(allocator, vm_id, vm_incarnation,
                            WVM_VM_NAMESPACE_QUARANTINED, &record, error,
                            error_len) != 0) {
        return -1;
    }
    record->state = WVM_VM_NAMESPACE_FREE;
    record->current_vm_incarnation = 0;
    return 0;
}

int wvm_vm_namespace_restore(
    struct wvm_vm_namespace_allocator *allocator,
    enum wvm_namespace_abi namespace_abi, uint32_t vm_id,
    uint64_t vm_incarnation, enum wvm_vm_namespace_state state, char *error,
    size_t error_len)
{
    struct wvm_vm_namespace_record *record;

    if (!allocator || !allocator->records || allocator->record_capacity == 0 ||
        !valid_namespace_abi(namespace_abi) || vm_id == 0 ||
        vm_incarnation == 0 || vm_incarnation == UINT64_MAX ||
        !valid_namespace_state(state) || state == WVM_VM_NAMESPACE_FREE ||
        (namespace_abi == WVM_NAMESPACE_ABI_LEGACY && vm_id > 255) ||
        (namespace_abi == WVM_NAMESPACE_ABI_U32 && vm_id < 256)) {
        set_error(error, error_len, "invalid durable VM namespace record");
        return -1;
    }

    record = find_mutable(allocator, vm_id);
    if (record) {
        if (record->namespace_abi != namespace_abi ||
            record->current_vm_incarnation != vm_incarnation ||
            record->state != state) {
            set_error(error, error_len,
                      "durable VM namespace %u conflicts with local state",
                      vm_id);
            return -1;
        }
        return 0;
    }
    if (allocator->record_count == allocator->record_capacity) {
        set_error(error, error_len, "VM namespace recovery capacity is exhausted");
        return -1;
    }

    record = &allocator->records[allocator->record_count++];
    memset(record, 0, sizeof(*record));
    record->vm_id = vm_id;
    record->next_vm_incarnation = vm_incarnation + 1U;
    record->current_vm_incarnation = vm_incarnation;
    record->state = state;
    record->namespace_abi = namespace_abi;
    record->legacy_cluster_epoch =
        namespace_abi == WVM_NAMESPACE_ABI_LEGACY
            ? allocator->legacy_cluster_epoch
            : 0;

    if (namespace_abi == WVM_NAMESPACE_ABI_LEGACY) {
        if (vm_id >= allocator->next_legacy_vm_id) {
            allocator->next_legacy_vm_id = vm_id == 255 ? 1 : vm_id + 1U;
        }
    } else if (vm_id >= allocator->next_vm_id) {
        allocator->next_vm_id = vm_id == UINT32_MAX ? 256 : vm_id + 1U;
    }
    return 0;
}

int wvm_local_name_namespace_validate(
    const struct wvm_local_name_namespace *namespace_value, char *error,
    size_t error_len)
{
    size_t bytes;
    size_t i;

    if (!namespace_value || namespace_value->name_generation == 0 ||
        digest_is_zero(namespace_value->derivation_salt_digest)) {
        set_error(error, error_len, "local namespace has invalid metadata");
        return -1;
    }

    bytes = strnlen(namespace_value->namespace_name,
                    sizeof(namespace_value->namespace_name));
    if (bytes == 0 || bytes > WVM_LOCAL_NAMESPACE_MAX_BYTES ||
        namespace_value->namespace_name[bytes] != '\0') {
        set_error(error, error_len, "local namespace has invalid text length");
        return -1;
    }
    for (i = 0; i < bytes; i++) {
        unsigned char c = (unsigned char)namespace_value->namespace_name[i];

        if (c < 0x21 || c > 0x7e || c == '/' || c == '\\') {
            set_error(error, error_len,
                      "local namespace contains an unsafe character");
            return -1;
        }
    }
    return 0;
}

int wvm_local_name_namespace_derive(
    const struct wvm_local_name_identity *identity,
    struct wvm_local_name_namespace *namespace_out, char *error,
    size_t error_len)
{
    uint8_t derivation_input[4 + 8 + 8 + 4 + WVM_IDENTITY_ID_BYTES +
                             WVM_IDENTITY_ID_BYTES];
    char digest_prefix[17];
    static const char hex[] = "0123456789abcdef";
    size_t i;
    int written;

    if (!identity || !namespace_out || identity->vm_id == 0 ||
        identity->vm_incarnation == 0 || identity->manifest_generation == 0 ||
        identity->physical_node_id == 0 ||
        bytes_are_zero(identity->manifest_id, sizeof(identity->manifest_id)) ||
        bytes_are_zero(identity->admission_tx_id,
                       sizeof(identity->admission_tx_id))) {
        set_error(error, error_len, "invalid local namespace identity");
        return -1;
    }

    memset(namespace_out, 0, sizeof(*namespace_out));
    write_be32(derivation_input, identity->vm_id);
    write_be64(derivation_input + 4, identity->vm_incarnation);
    write_be64(derivation_input + 12, identity->manifest_generation);
    write_be32(derivation_input + 20, identity->physical_node_id);
    memcpy(derivation_input + 24, identity->manifest_id,
           sizeof(identity->manifest_id));
    memcpy(derivation_input + 24 + sizeof(identity->manifest_id),
           identity->admission_tx_id, sizeof(identity->admission_tx_id));
    wvm_sha256_digest(derivation_input, sizeof(derivation_input),
                      namespace_out->derivation_salt_digest);

    for (i = 0; i < 8; i++) {
        digest_prefix[i * 2] =
            hex[namespace_out->derivation_salt_digest[i] >> 4];
        digest_prefix[i * 2 + 1] =
            hex[namespace_out->derivation_salt_digest[i] & 0x0f];
    }
    digest_prefix[sizeof(digest_prefix) - 1] = '\0';

    written = snprintf(namespace_out->namespace_name,
                       sizeof(namespace_out->namespace_name),
                       "wvm-v%" PRIu32 "-i%" PRIu64 "-g%" PRIu64
                       "-n%" PRIu32 "-%s",
                       identity->vm_id, identity->vm_incarnation,
                       identity->manifest_generation,
                       identity->physical_node_id, digest_prefix);
    if (written < 0 ||
        (size_t)written >= sizeof(namespace_out->namespace_name)) {
        set_error(error, error_len, "derived local namespace is too long");
        return -1;
    }
    namespace_out->name_generation = identity->manifest_generation;
    return wvm_local_name_namespace_validate(namespace_out, error, error_len);
}

int wvm_local_name_namespace_validate_unique(
    const struct wvm_local_name_namespace *namespaces, size_t namespace_count,
    char *error, size_t error_len)
{
    size_t i;
    size_t j;

    if (!namespaces && namespace_count != 0) {
        set_error(error, error_len, "local namespace list is missing");
        return -1;
    }
    for (i = 0; i < namespace_count; i++) {
        if (wvm_local_name_namespace_validate(&namespaces[i], error,
                                              error_len) != 0) {
            return -1;
        }
        for (j = 0; j < i; j++) {
            if (strcmp(namespaces[i].namespace_name,
                       namespaces[j].namespace_name) == 0) {
                set_error(error, error_len,
                          "local namespace collision: %s",
                          namespaces[i].namespace_name);
                return -1;
            }
        }
    }
    return 0;
}
