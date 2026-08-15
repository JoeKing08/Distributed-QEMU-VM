#define _GNU_SOURCE

#include "wavevm_route_delivery.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "wavevm_sha256.h"

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
            errno = EIO;
            return -1;
        }
        offset += (size_t)received;
    }
    return 0;
}

static int key_equal(const struct wvm_route_snapshot_key *left,
                     const struct wvm_route_snapshot_key *right)
{
    return left && right &&
           left->scope_key.vm_id == right->scope_key.vm_id &&
           left->scope_key.vm_incarnation == right->scope_key.vm_incarnation &&
           left->scope_key.route_scope_id == right->scope_key.route_scope_id &&
           left->topology_revision == right->topology_revision &&
           left->route_generation == right->route_generation &&
           memcmp(left->snapshot_digest, right->snapshot_digest,
                  sizeof(left->snapshot_digest)) == 0;
}

static int path_parent(const char *path, char *parent, size_t capacity)
{
    const char *slash;
    size_t length;

    if (!path || !parent || capacity == 0) {
        return -1;
    }
    slash = strrchr(path, '/');
    if (!slash) {
        return snprintf(parent, capacity, ".") < (int)capacity ? 0 : -1;
    }
    length = (size_t)(slash - path);
    if (length == 0) {
        length = 1;
    }
    if (length + 1 > capacity) {
        return -1;
    }
    memcpy(parent, path, length);
    parent[length] = '\0';
    return 0;
}

static int encode_snapshot_alloc(
    const struct wvm_route_snapshot_record *snapshot, uint8_t **bytes_out,
    size_t *byte_count_out, char *error, size_t error_len)
{
    size_t capacity = 4096;

    if (!snapshot || !bytes_out || !byte_count_out) {
        set_error(error, error_len, "route snapshot publish input is missing");
        return -1;
    }
    while (capacity <= 4U * 1024U * 1024U) {
        uint8_t *bytes = malloc(capacity);
        size_t byte_count = 0;
        uint8_t digest[WVM_SHA256_DIGEST_BYTES];

        if (!bytes) {
            set_error(error, error_len, "cannot allocate route snapshot");
            return -1;
        }
        if (wvm_route_snapshot_record_encode(
                snapshot, bytes, capacity, &byte_count, digest, error,
                error_len) == 0) {
            *bytes_out = bytes;
            *byte_count_out = byte_count;
            return 0;
        }
        free(bytes);
        if (capacity == 4U * 1024U * 1024U) {
            break;
        }
        capacity *= 2U;
        if (capacity > 4U * 1024U * 1024U) {
            capacity = 4U * 1024U * 1024U;
        }
    }
    set_error(error, error_len, "route snapshot exceeds delivery limit");
    return -1;
}

void wvm_route_snapshot_file_storage_init(
    struct wvm_route_snapshot_file_storage *storage)
{
    if (storage) {
        memset(storage, 0, sizeof(*storage));
    }
}

void wvm_route_snapshot_file_storage_free(
    struct wvm_route_snapshot_file_storage *storage)
{
    if (!storage) {
        return;
    }
    free(storage->rules);
    free(storage->ack_entries);
    memset(storage, 0, sizeof(*storage));
}

int wvm_route_snapshot_path_from_manifest(
    const char *manifest_path, char *route_path, size_t route_path_capacity,
    char *error, size_t error_len)
{
    int written;

    if (!manifest_path || manifest_path[0] == '\0' || !route_path ||
        route_path_capacity == 0) {
        set_error(error, error_len, "manifest route path input is invalid");
        return -1;
    }
    written = snprintf(route_path, route_path_capacity, "%s.route",
                       manifest_path);
    if (written < 0 || (size_t)written >= route_path_capacity ||
        (size_t)written >= WVM_ROUTE_DELIVERY_PATH_MAX) {
        set_error(error, error_len, "derived route snapshot path is too long");
        return -1;
    }
    return 0;
}

int wvm_route_snapshot_file_publish(
    const char *path, const struct wvm_route_snapshot_record *snapshot,
    char *error, size_t error_len)
{
    uint8_t *bytes = NULL;
    size_t byte_count = 0;
    char temporary[WVM_ROUTE_DELIVERY_PATH_MAX];
    char parent[WVM_ROUTE_DELIVERY_PATH_MAX];
    int fd = -1;
    int directory_fd = -1;
    int result = -1;
    int written;

    temporary[0] = '\0';
    if (!path || path[0] == '\0') {
        set_error(error, error_len, "route snapshot path is invalid");
        return -1;
    }
    if (encode_snapshot_alloc(snapshot, &bytes, &byte_count, error,
                              error_len) != 0 ||
        path_parent(path, parent, sizeof(parent)) != 0) {
        goto out;
    }
    written = snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path,
                       (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(temporary)) {
        set_error(error, error_len, "temporary route snapshot path is too long");
        goto out;
    }
    fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        set_error(error, error_len, "cannot create route snapshot: %s",
                  strerror(errno));
        goto out;
    }
    if (write_full(fd, bytes, byte_count) != 0 || fsync(fd) != 0 ||
        close(fd) != 0) {
        fd = -1;
        set_error(error, error_len, "cannot fsync route snapshot: %s",
                  strerror(errno));
        goto out;
    }
    fd = -1;
    if (rename(temporary, path) != 0) {
        set_error(error, error_len, "cannot activate route snapshot: %s",
                  strerror(errno));
        goto out;
    }
    directory_fd = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0 || fsync(directory_fd) != 0) {
        set_error(error, error_len, "cannot fsync route snapshot directory: %s",
                  strerror(errno));
        goto out;
    }
    result = 0;
out:
    if (fd >= 0) {
        close(fd);
    }
    if (directory_fd >= 0) {
        close(directory_fd);
    }
    if (result != 0 && temporary[0] != '\0') {
        unlink(temporary);
    }
    free(bytes);
    return result;
}

int wvm_route_snapshot_file_load(
    const char *path, struct wvm_route_snapshot_file_storage *storage,
    char *error, size_t error_len)
{
    struct stat st;
    uint8_t *bytes = NULL;
    size_t byte_count;
    int fd = -1;
    int result = -1;

    if (!path || !storage || stat(path, &st) != 0 || st.st_size <= 0 ||
        (uintmax_t)st.st_size > 4U * 1024U * 1024U) {
        set_error(error, error_len, "route snapshot file size is invalid");
        return -1;
    }
    bytes = malloc((size_t)st.st_size);
    if (!bytes) {
        set_error(error, error_len, "cannot allocate route snapshot file");
        return -1;
    }
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0 || read_full(fd, bytes, (size_t)st.st_size) != 0) {
        set_error(error, error_len, "cannot read route snapshot: %s",
                  strerror(errno));
        goto out;
    }
    byte_count = (size_t)st.st_size;
    wvm_route_snapshot_file_storage_free(storage);
    storage->rules = calloc(WVM_ROUTE_DELIVERY_MAX_ENTRIES,
                            sizeof(*storage->rules));
    storage->ack_entries = calloc(WVM_ROUTE_DELIVERY_MAX_ENTRIES,
                                  sizeof(*storage->ack_entries));
    if (!storage->rules || !storage->ack_entries) {
        set_error(error, error_len, "cannot allocate route snapshot entries");
        goto out;
    }
    storage->snapshot.next_hop_rules.entries = storage->rules;
    storage->snapshot.next_hop_rules.capacity = WVM_ROUTE_DELIVERY_MAX_ENTRIES;
    storage->snapshot.required_ack_set.entries.entries = storage->ack_entries;
    storage->snapshot.required_ack_set.entries.capacity =
        WVM_ROUTE_DELIVERY_MAX_ENTRIES;
    if (wvm_route_snapshot_record_decode(bytes, byte_count, &storage->snapshot,
                                         error, error_len) != 0) {
        goto out;
    }
    result = 0;
out:
    if (fd >= 0) {
        close(fd);
    }
    free(bytes);
    if (result != 0) {
        wvm_route_snapshot_file_storage_free(storage);
    }
    return result;
}

int wvm_route_snapshot_file_matches(
    const struct wvm_route_snapshot_file_storage *storage,
    const struct wvm_route_snapshot_key *expected_key, char *error,
    size_t error_len)
{
    if (!storage || !expected_key ||
        wvm_route_snapshot_record_validate(&storage->snapshot, error,
                                           error_len) != 0 ||
        !key_equal(&storage->snapshot.route_snapshot_key, expected_key)) {
        set_error(error, error_len,
                  "route snapshot does not match admitted snapshot key");
        return -1;
    }
    return 0;
}
