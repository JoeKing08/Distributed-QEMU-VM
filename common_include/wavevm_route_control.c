#define _GNU_SOURCE

#include "wavevm_route_control.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "wavevm_canonical.h"
#include "wavevm_route_delivery.h"

#define WVM_ROUTE_CONTROL_JOURNAL_VERSION 1U
#define WVM_ROUTE_CONTROL_JOURNAL_HEADER_BYTES 56U

static const uint8_t route_control_journal_magic[8] = {
    'W', 'V', 'M', 'R', 'C', 'T', 'L', '1',
};

struct wvm_route_control_operation {
    uint16_t message_type;
    uint8_t operation_id[WVM_IDENTITY_ID_BYTES];
    uint8_t semantic_payload_digest[WVM_SHA256_DIGEST_BYTES];
    struct wvm_route_control_result result;
};

struct route_snapshot_storage {
    struct wvm_route_snapshot_record snapshot;
    struct wvm_route_rule_record *rules;
    struct wvm_required_ack_entry *ack_entries;
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

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void write_be64(uint8_t *bytes, uint64_t value)
{
    size_t i;

    for (i = 0; i < 8; i++) {
        bytes[7U - i] = (uint8_t)(value >> (i * 8U));
    }
}

static uint16_t read_be16(const uint8_t *bytes)
{
    return ((uint16_t)bytes[0] << 8) | bytes[1];
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static uint64_t read_be64(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t i;

    for (i = 0; i < 8; i++) {
        value = (value << 8) | bytes[i];
    }
    return value;
}

static int write_full(int fd, const uint8_t *bytes, size_t byte_count)
{
    size_t offset = 0;

    while (offset < byte_count) {
        ssize_t written = write(fd, bytes + offset, byte_count - offset);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            errno = EIO;
            return -1;
        }
        offset += (size_t)written;
    }
    return 0;
}

/*
 * Returns one for a full buffer, zero for clean EOF before any bytes, and
 * minus one for a torn or unreadable record.
 */
static int read_full(int fd, uint8_t *bytes, size_t byte_count)
{
    size_t offset = 0;

    while (offset < byte_count) {
        ssize_t received = read(fd, bytes + offset, byte_count - offset);

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (received == 0) {
            return offset == 0 ? 0 : -1;
        }
        offset += (size_t)received;
    }
    return 1;
}

static int operation_type_valid(uint16_t message_type)
{
    return message_type == WVM_ENVELOPE_V1_MSG_ROUTE_PREPARE ||
           message_type == WVM_ENVELOPE_V1_MSG_ROUTE_COMMIT ||
           message_type == WVM_ENVELOPE_V1_MSG_ROUTE_RETIRE;
}

static int request_matches_route_key(
    const struct wvm_envelope_v1 *request,
    const struct wvm_route_snapshot_key *key, char *error, size_t error_len)
{
    if (!request || !key || request->vm_id != key->scope_key.vm_id ||
        request->vm_incarnation != key->scope_key.vm_incarnation) {
        set_error(error, error_len,
                  "route control envelope does not match route scope");
        return -1;
    }
    return 0;
}

static void route_snapshot_storage_free(struct route_snapshot_storage *storage)
{
    if (!storage) {
        return;
    }
    free(storage->rules);
    free(storage->ack_entries);
    memset(storage, 0, sizeof(*storage));
}

static int route_snapshot_list_counts(const uint8_t *bytes, size_t byte_count,
                                      size_t *rule_count_out,
                                      size_t *ack_count_out, char *error,
                                      size_t error_len)
{
    struct wvm_canonical_record record;
    struct wvm_canonical_field field;
    const uint8_t *rule_list = NULL;
    const uint8_t *ack_record = NULL;
    size_t rule_list_bytes = 0;
    size_t ack_record_bytes = 0;
    size_t offset = 0;
    int next;

    if (!bytes || !rule_count_out || !ack_count_out ||
        wvm_canonical_record_parse(bytes, byte_count, &record) != 0 ||
        record.record_type != WVM_RECORD_ROUTE_SNAPSHOT) {
        set_error(error, error_len, "route prepare payload is not a snapshot");
        return -1;
    }
    *rule_count_out = 0;
    *ack_count_out = 0;
    while ((next = wvm_canonical_record_next(&record, &offset, &field)) > 0) {
        if (field.tag == 4) {
            rule_list = field.value;
            rule_list_bytes = field.value_bytes;
        } else if (field.tag == 5) {
            ack_record = field.value;
            ack_record_bytes = field.value_bytes;
        }
    }
    if (next < 0 || !rule_list || rule_list_bytes < 4 || !ack_record ||
        wvm_canonical_record_parse(ack_record, ack_record_bytes, &record) !=
            0 ||
        record.record_type != WVM_RECORD_REQUIRED_ACK_SET) {
        set_error(error, error_len, "route snapshot list encoding is invalid");
        return -1;
    }
    *rule_count_out = read_be32(rule_list);
    offset = 0;
    while ((next = wvm_canonical_record_next(&record, &offset, &field)) > 0) {
        if (field.tag == 1) {
            if (field.value_bytes < 4) {
                set_error(error, error_len, "route ACK list is invalid");
                return -1;
            }
            *ack_count_out = read_be32(field.value);
            break;
        }
    }
    if (next < 0 || *rule_count_out == 0 || *ack_count_out == 0 ||
        *rule_count_out > WVM_ROUTE_RUNTIME_MAX_ENTRIES ||
        *ack_count_out > WVM_ROUTE_RUNTIME_MAX_ENTRIES) {
        set_error(error, error_len, "route snapshot list exceeds limits");
        return -1;
    }
    return 0;
}

static int route_snapshot_decode_alloc(const uint8_t *bytes, size_t byte_count,
                                       struct route_snapshot_storage *storage,
                                       char *error, size_t error_len)
{
    size_t rule_count;
    size_t ack_count;

    if (!storage ||
        route_snapshot_list_counts(bytes, byte_count, &rule_count, &ack_count,
                                   error, error_len) != 0) {
        return -1;
    }
    memset(storage, 0, sizeof(*storage));
    storage->rules = calloc(rule_count, sizeof(*storage->rules));
    storage->ack_entries = calloc(ack_count, sizeof(*storage->ack_entries));
    if (!storage->rules || !storage->ack_entries) {
        set_error(error, error_len, "cannot allocate route snapshot lists");
        route_snapshot_storage_free(storage);
        return -1;
    }
    storage->snapshot.next_hop_rules.entries = storage->rules;
    storage->snapshot.next_hop_rules.capacity = rule_count;
    storage->snapshot.required_ack_set.entries.entries = storage->ack_entries;
    storage->snapshot.required_ack_set.entries.capacity = ack_count;
    if (wvm_route_snapshot_record_decode(bytes, byte_count, &storage->snapshot,
                                         error, error_len) != 0) {
        route_snapshot_storage_free(storage);
        return -1;
    }
    return 0;
}

static struct wvm_route_control_operation *find_operation(
    struct wvm_route_control *control, const struct wvm_envelope_v1 *request)
{
    size_t i;

    for (i = 0; i < control->operation_count; i++) {
        struct wvm_route_control_operation *operation =
            &control->operations[i];

        if (operation->message_type == request->message_type &&
            memcmp(operation->operation_id, request->operation_id,
                   sizeof(operation->operation_id)) == 0) {
            return operation;
        }
    }
    return NULL;
}

static int remember_operation(struct wvm_route_control *control,
                              const struct wvm_envelope_v1 *request,
                              const struct wvm_route_control_result *result,
                              char *error, size_t error_len)
{
    struct wvm_route_control_operation *operations;
    size_t new_capacity;
    struct wvm_route_control_operation *operation;

    if (control->operation_count == WVM_ROUTE_CONTROL_MAX_OPERATIONS) {
        set_error(error, error_len, "route control operation capacity is full");
        return -1;
    }
    if (control->operation_count == control->operation_capacity) {
        new_capacity = control->operation_capacity
                           ? control->operation_capacity * 2U
                           : 16U;
        if (new_capacity > WVM_ROUTE_CONTROL_MAX_OPERATIONS) {
            new_capacity = WVM_ROUTE_CONTROL_MAX_OPERATIONS;
        }
        operations = realloc(control->operations,
                             new_capacity * sizeof(*operations));
        if (!operations) {
            set_error(error, error_len,
                      "cannot allocate route control operation table");
            return -1;
        }
        control->operations = operations;
        control->operation_capacity = new_capacity;
    }
    operation = &control->operations[control->operation_count++];
    memset(operation, 0, sizeof(*operation));
    operation->message_type = request->message_type;
    memcpy(operation->operation_id, request->operation_id,
           sizeof(operation->operation_id));
    memcpy(operation->semantic_payload_digest,
           request->semantic_payload_digest,
           sizeof(operation->semantic_payload_digest));
    operation->result = *result;
    return 0;
}

static int apply_unlogged(struct wvm_route_control *control,
                          const struct wvm_envelope_v1 *request, char *error,
                          size_t error_len,
                          struct wvm_route_control_result *result_out)
{
    struct route_snapshot_storage storage;
    struct wvm_route_snapshot_key key;
    int result;

    if (!control || !control->runtime || !request ||
        !operation_type_valid(request->message_type)) {
        set_error(error, error_len, "route control request is invalid");
        return -1;
    }
    if (request->message_type == WVM_ENVELOPE_V1_MSG_ROUTE_PREPARE) {
        memset(&storage, 0, sizeof(storage));
        if (route_snapshot_decode_alloc(request->payload, request->payload_bytes,
                                        &storage, error, error_len) != 0 ||
            request_matches_route_key(request,
                                      &storage.snapshot.route_snapshot_key,
                                      error, error_len) != 0) {
            route_snapshot_storage_free(&storage);
            return -1;
        }
        result = wvm_route_runtime_prepare(control->runtime, &storage.snapshot,
                                           error, error_len);
        if (result == 0 && result_out) {
            result_out->recorded_state = 1;
            result_out->route_snapshot_key =
                storage.snapshot.route_snapshot_key;
            result_out->operation_retention_horizon_ms =
                storage.snapshot.operation_retention_horizon_ms;
        }
        route_snapshot_storage_free(&storage);
        return result;
    }
    if (wvm_route_snapshot_key_decode(request->payload, request->payload_bytes,
                                      &key, error, error_len) != 0 ||
        request_matches_route_key(request, &key, error, error_len) != 0) {
        return -1;
    }
    if (request->message_type == WVM_ENVELOPE_V1_MSG_ROUTE_COMMIT) {
        result = wvm_route_runtime_activate(control->runtime, &key, error,
                                            error_len);
        if (result == 0 && result_out) {
            result_out->recorded_state = 2;
            result_out->route_snapshot_key = key;
        }
        return result;
    }
    result = wvm_route_runtime_retire(control->runtime, &key, error,
                                      error_len);
    if (result == 0 && result_out) {
        result_out->recorded_state = 4;
        result_out->route_snapshot_key = key;
    }
    return result;
}

static int encode_local_frame(const struct wvm_envelope_v1 *request,
                              uint8_t **frame_out, size_t *frame_bytes_out,
                              char *error, size_t error_len)
{
    uint8_t *frame;
    size_t frame_bytes = 0;

    frame = malloc(WVM_ROUTE_CONTROL_MAX_FRAME_BYTES);
    if (!frame) {
        set_error(error, error_len, "cannot allocate route control frame");
        return -1;
    }
    if (wvm_envelope_v1_encode(request, WVM_ENVELOPE_V1_TRANSPORT_LOCAL,
                               frame, WVM_ROUTE_CONTROL_MAX_FRAME_BYTES,
                               &frame_bytes, error, error_len) != 0) {
        free(frame);
        return -1;
    }
    *frame_out = frame;
    *frame_bytes_out = frame_bytes;
    return 0;
}

static int journal_append(struct wvm_route_control *control,
                          const uint8_t *frame, size_t frame_bytes, char *error,
                          size_t error_len)
{
    uint8_t header[WVM_ROUTE_CONTROL_JOURNAL_HEADER_BYTES];
    uint8_t digest[WVM_SHA256_DIGEST_BYTES];
    uint64_t sequence;

    if (!control || control->journal_fd < 0 || !frame ||
        frame_bytes == 0 || frame_bytes > WVM_ROUTE_CONTROL_MAX_FRAME_BYTES ||
        control->next_sequence == 0) {
        set_error(error, error_len, "route control journal input is invalid");
        return -1;
    }
    sequence = control->next_sequence;
    memset(header, 0, sizeof(header));
    memcpy(header, route_control_journal_magic,
           sizeof(route_control_journal_magic));
    write_be16(header + 8, WVM_ROUTE_CONTROL_JOURNAL_VERSION);
    write_be64(header + 12, sequence);
    write_be32(header + 20, (uint32_t)frame_bytes);
    wvm_sha256_digest(frame, frame_bytes, digest);
    memcpy(header + 24, digest, sizeof(digest));
    if (lseek(control->journal_fd, 0, SEEK_END) < 0 ||
        write_full(control->journal_fd, header, sizeof(header)) != 0 ||
        write_full(control->journal_fd, frame, frame_bytes) != 0 ||
        fsync(control->journal_fd) != 0) {
        set_error(error, error_len, "cannot persist route control journal: %s",
                  strerror(errno));
        return -1;
    }
    control->next_sequence = sequence + 1U;
    return 0;
}

static int apply_and_record(struct wvm_route_control *control,
                            const struct wvm_envelope_v1 *request,
                            const uint8_t *frame, size_t frame_bytes,
                            int replaying,
                            struct wvm_route_control_result *result_out,
                            char *error, size_t error_len)
{
    struct wvm_route_control_operation *existing;
    struct wvm_route_control_result result;

    existing = find_operation(control, request);
    if (existing) {
        if (memcmp(existing->semantic_payload_digest,
                   request->semantic_payload_digest,
                   sizeof(existing->semantic_payload_digest)) != 0) {
            set_error(error, error_len,
                      "route control operation ID conflicts with payload");
            return -1;
        }
        if (result_out) {
            *result_out = existing->result;
        }
        return 0;
    }
    /*
     * Persist first. A successful response is never emitted for an
     * in-memory-only route transition; replay finishes a transition after a
     * process restart. If applying now fails, the caller receives failure and
     * no data path uses the new snapshot until replay/duplicate succeeds.
     */
    if (!replaying &&
        journal_append(control, frame, frame_bytes, error, error_len) != 0) {
        return -1;
    }
    memset(&result, 0, sizeof(result));
    if (apply_unlogged(control, request, error, error_len, &result) != 0 ||
        remember_operation(control, request, &result, error, error_len) !=
            0) {
        return -1;
    }
    if (result_out) {
        *result_out = result;
    }
    return 0;
}

int wvm_route_control_open(struct wvm_route_control *control,
                           struct wvm_route_runtime *runtime,
                           const char *journal_path, char *error,
                           size_t error_len)
{
    uint64_t expected_sequence = 1;
    off_t valid_end = 0;

    if (!control || !runtime || !journal_path || journal_path[0] == '\0') {
        set_error(error, error_len, "route control initialization is invalid");
        return -1;
    }
    memset(control, 0, sizeof(*control));
    control->journal_fd = -1;
    control->runtime = runtime;
    pthread_mutex_init(&control->lock, NULL);
    control->journal_fd =
        open(journal_path, O_RDWR | O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (control->journal_fd < 0) {
        set_error(error, error_len, "cannot open route control journal: %s",
                  strerror(errno));
        wvm_route_control_close(control);
        return -1;
    }
    for (;;) {
        uint8_t header[WVM_ROUTE_CONTROL_JOURNAL_HEADER_BYTES];
        uint8_t digest[WVM_SHA256_DIGEST_BYTES];
        uint8_t *frame = NULL;
        uint32_t frame_bytes;
        uint64_t sequence;
        struct wvm_envelope_v1 request;
        int read_result = read_full(control->journal_fd, header, sizeof(header));

        if (read_result == 0) {
            break;
        }
        if (read_result < 0 ||
            memcmp(header, route_control_journal_magic,
                   sizeof(route_control_journal_magic)) != 0 ||
            read_be16(header + 8) != WVM_ROUTE_CONTROL_JOURNAL_VERSION ||
            read_be16(header + 10) != 0 ||
            (sequence = read_be64(header + 12)) != expected_sequence ||
            (frame_bytes = read_be32(header + 20)) == 0 ||
            frame_bytes > WVM_ROUTE_CONTROL_MAX_FRAME_BYTES) {
            if (read_result < 0) {
                break;
            }
            set_error(error, error_len, "route control journal header is invalid");
            wvm_route_control_close(control);
            return -1;
        }
        frame = malloc(frame_bytes);
        if (!frame || read_full(control->journal_fd, frame, frame_bytes) != 1) {
            free(frame);
            break;
        }
        wvm_sha256_digest(frame, frame_bytes, digest);
        if (memcmp(digest, header + 24, sizeof(digest)) != 0 ||
            wvm_envelope_v1_decode(frame, frame_bytes,
                                   WVM_ENVELOPE_V1_TRANSPORT_LOCAL, &request,
                                   error, error_len) != 0 ||
            !operation_type_valid(request.message_type) ||
            apply_and_record(control, &request, frame, frame_bytes, 1, NULL,
                             error, error_len) != 0) {
            free(frame);
            wvm_route_control_close(control);
            return -1;
        }
        free(frame);
        valid_end = lseek(control->journal_fd, 0, SEEK_CUR);
        expected_sequence++;
    }
    if (ftruncate(control->journal_fd, valid_end) != 0 ||
        lseek(control->journal_fd, 0, SEEK_END) < 0) {
        set_error(error, error_len, "cannot finalize route control journal: %s",
                  strerror(errno));
        wvm_route_control_close(control);
        return -1;
    }
    control->next_sequence = expected_sequence;
    return 0;
}

void wvm_route_control_close(struct wvm_route_control *control)
{
    if (!control) {
        return;
    }
    if (control->journal_fd >= 0) {
        close(control->journal_fd);
    }
    free(control->operations);
    if (control->runtime) {
        pthread_mutex_destroy(&control->lock);
    }
    memset(control, 0, sizeof(*control));
    control->journal_fd = -1;
}

int wvm_route_control_apply(struct wvm_route_control *control,
                            const struct wvm_envelope_v1 *request,
                            struct wvm_route_control_result *result_out,
                            char *error, size_t error_len)
{
    uint8_t *frame = NULL;
    size_t frame_bytes = 0;
    int result;

    if (!control || control->journal_fd < 0 || !request ||
        !operation_type_valid(request->message_type)) {
        set_error(error, error_len, "route control request is unsupported");
        return -1;
    }
    if (encode_local_frame(request, &frame, &frame_bytes, error, error_len) !=
        0) {
        return -1;
    }
    pthread_mutex_lock(&control->lock);
    result = apply_and_record(control, request, frame, frame_bytes, 0,
                              result_out, error, error_len);
    pthread_mutex_unlock(&control->lock);
    free(frame);
    return result;
}
