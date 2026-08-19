#include "wavevm_runtime_names.h"

#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static int format_name(char *destination, size_t destination_bytes,
                       const char *format, const char *namespace_name)
{
    int written;

    written = snprintf(destination, destination_bytes, format, namespace_name);
    return written < 0 || (size_t)written >= destination_bytes ? -1 : 0;
}

static int write_ready_bytes(int fd, const void *bytes, size_t byte_count)
{
    const uint8_t *cursor = bytes;
    size_t offset = 0;

    while (offset < byte_count) {
        ssize_t written = write(fd, cursor + offset, byte_count - offset);

        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return -1;
        }
        offset += (size_t)written;
    }
    return 0;
}

static int read_ready_bytes(int fd, void *bytes, size_t byte_count)
{
    uint8_t *cursor = bytes;
    size_t offset = 0;

    while (offset < byte_count) {
        ssize_t received = read(fd, cursor + offset, byte_count - offset);

        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            return -1;
        }
        offset += (size_t)received;
    }
    return 0;
}

static int runtime_ready_record_fill(
    const struct wvm_node_runtime_manifest *manifest,
    uint64_t node_instance_id, struct wvm_runtime_ready_record *record,
    char *error, size_t error_len)
{
    if (!manifest || !record || node_instance_id == 0 ||
        node_instance_id != manifest->expected_node_instance_id ||
        !manifest->has_activation_fence ||
        wvm_node_runtime_manifest_validate(manifest, error, error_len) != 0) {
        set_error(error, error_len,
                  "runtime readiness identity is not an admitted manifest");
        return -1;
    }
    memset(record, 0, sizeof(*record));
    record->magic = WVM_RUNTIME_READY_MAGIC;
    record->version = WVM_RUNTIME_READY_VERSION;
    record->vm_id = manifest->vm_id;
    record->physical_node_id = manifest->physical_node_id;
    record->vm_incarnation = manifest->vm_incarnation;
    record->manifest_generation = manifest->manifest_generation;
    record->node_instance_id = node_instance_id;
    memcpy(record->candidate_manifest_digest,
           manifest->candidate_manifest_digest,
           sizeof(record->candidate_manifest_digest));
    return 0;
}

int wvm_runtime_name_set_validate(const struct wvm_runtime_name_set *names,
                                  char *error, size_t error_len)
{
    const char *values[] = {
        names ? names->runtime_socket : NULL,
        names ? names->executor_socket : NULL,
        names ? names->worker_socket : NULL,
        names ? names->monitor_socket : NULL,
        names ? names->ready_file : NULL,
        names ? names->shm_name : NULL,
        names ? names->log_directory : NULL,
        names ? names->temporary_directory : NULL,
    };
    size_t i;
    size_t j;

    if (!names) {
        set_error(error, error_len, "runtime name set is missing");
        return -1;
    }
    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        if (!values[i] || values[i][0] == '\0' ||
            strnlen(values[i], WVM_RUNTIME_PATH_MAX) >=
                WVM_RUNTIME_PATH_MAX) {
            set_error(error, error_len, "runtime name %zu is invalid", i);
            return -1;
        }
        for (j = 0; j < i; j++) {
            if (strcmp(values[i], values[j]) == 0) {
                set_error(error, error_len,
                          "runtime name collision between entries %zu and %zu",
                          j, i);
                return -1;
            }
        }
    }
    if (names->shm_name[0] != '/' ||
        strchr(names->shm_name + 1, '/') != NULL) {
        set_error(error, error_len, "SHM name is not a valid POSIX name");
        return -1;
    }
    return 0;
}

int wvm_runtime_name_set_derive(
    const struct wvm_local_name_namespace *namespace_value,
    struct wvm_runtime_name_set *names, char *error, size_t error_len)
{
    if (!namespace_value || !names) {
        set_error(error, error_len, "runtime namespace or name set is missing");
        return -1;
    }
    if (wvm_local_name_namespace_validate(namespace_value, error, error_len) !=
        0) {
        return -1;
    }
    memset(names, 0, sizeof(*names));
    if (format_name(names->runtime_socket, sizeof(names->runtime_socket),
                    "/tmp/wvm_user_%s.sock", namespace_value->namespace_name) !=
            0 ||
        format_name(names->executor_socket, sizeof(names->executor_socket),
                    "/tmp/%s-executor.sock", namespace_value->namespace_name) !=
            0 ||
        format_name(names->worker_socket, sizeof(names->worker_socket),
                    "/tmp/%s-worker.sock", namespace_value->namespace_name) !=
            0 ||
        format_name(names->monitor_socket, sizeof(names->monitor_socket),
                    "/tmp/%s-monitor.sock", namespace_value->namespace_name) !=
            0 ||
        format_name(names->ready_file, sizeof(names->ready_file),
                    "/tmp/%s-ready", namespace_value->namespace_name) != 0 ||
        format_name(names->shm_name, sizeof(names->shm_name),
                    "/wavevm_ram_%s", namespace_value->namespace_name) != 0 ||
        format_name(names->log_directory, sizeof(names->log_directory),
                    "/tmp/wavevm-%s-logs", namespace_value->namespace_name) !=
            0 ||
        format_name(names->temporary_directory,
                    sizeof(names->temporary_directory),
                    "/tmp/wavevm-%s-tmp", namespace_value->namespace_name) !=
            0) {
        set_error(error, error_len, "derived runtime name is too long");
        return -1;
    }
    return wvm_runtime_name_set_validate(names, error, error_len);
}

int wvm_runtime_ready_publish(
    const struct wvm_node_runtime_manifest *manifest,
    uint64_t node_instance_id, char *error, size_t error_len)
{
    struct wvm_runtime_name_set names;
    struct wvm_runtime_ready_record record;
    char temporary_path[WVM_RUNTIME_PATH_MAX];
    int fd = -1;
    int written;

    if (wvm_runtime_name_set_derive(
            manifest ? &manifest->local_names : NULL, &names, error,
            error_len) != 0 ||
        runtime_ready_record_fill(manifest, node_instance_id, &record, error,
                                   error_len) != 0) {
        return -1;
    }
    written = snprintf(temporary_path, sizeof(temporary_path), "%s.tmp.%ld",
                       names.ready_file, (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(temporary_path)) {
        set_error(error, error_len, "runtime readiness temporary path is too long");
        return -1;
    }
    unlink(temporary_path);
    fd = open(temporary_path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0 || write_ready_bytes(fd, &record, sizeof(record)) != 0 ||
        fsync(fd) != 0) {
        set_error(error, error_len, "cannot write runtime readiness: %s",
                  strerror(errno));
        if (fd >= 0) {
            close(fd);
        }
        unlink(temporary_path);
        return -1;
    }
    close(fd);
    if (link(temporary_path, names.ready_file) != 0) {
        int saved_errno = errno;

        unlink(temporary_path);
        if (saved_errno == EEXIST &&
            wvm_runtime_ready_validate(manifest, node_instance_id, error,
                                       error_len) == 0) {
            return 0;
        }
        set_error(error, error_len,
                  "cannot claim runtime readiness: %s",
                  strerror(saved_errno));
        return -1;
    }
    if (unlink(temporary_path) != 0 && errno != ENOENT) {
        set_error(error, error_len,
                  "cannot remove runtime readiness temporary file: %s",
                  strerror(errno));
        unlink(temporary_path);
        return -1;
    }
    return 0;
}

int wvm_runtime_ready_validate(
    const struct wvm_node_runtime_manifest *manifest,
    uint64_t node_instance_id, char *error, size_t error_len)
{
    struct wvm_runtime_name_set names;
    struct wvm_runtime_ready_record expected;
    struct wvm_runtime_ready_record actual;
    int fd;
    uint8_t extra;

    if (wvm_runtime_name_set_derive(
            manifest ? &manifest->local_names : NULL, &names, error,
            error_len) != 0 ||
        runtime_ready_record_fill(manifest, node_instance_id, &expected, error,
                                   error_len) != 0) {
        return -1;
    }
    fd = open(names.ready_file, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        set_error(error, error_len, "runtime readiness is unavailable: %s",
                  strerror(errno));
        return -EAGAIN;
    }
    if (read_ready_bytes(fd, &actual, sizeof(actual)) != 0 ||
        read(fd, &extra, sizeof(extra)) != 0 ||
        memcmp(&actual, &expected, sizeof(actual)) != 0) {
        close(fd);
        set_error(error, error_len,
                  "runtime readiness does not match admitted manifest");
        return -1;
    }
    close(fd);
    return 0;
}

int wvm_runtime_ready_remove(
    const struct wvm_node_runtime_manifest *manifest, char *error,
    size_t error_len)
{
    struct wvm_runtime_name_set names;

    if (wvm_runtime_name_set_derive(
            manifest ? &manifest->local_names : NULL, &names, error,
            error_len) != 0) {
        return -1;
    }
    if (access(names.ready_file, F_OK) == 0) {
        if (wvm_runtime_ready_validate(
                manifest, manifest->expected_node_instance_id, error,
                error_len) != 0) {
            set_error(error, error_len,
                      "runtime readiness is owned by another manifest");
            return -1;
        }
    } else if (errno != ENOENT) {
        set_error(error, error_len, "cannot inspect runtime readiness: %s",
                  strerror(errno));
        return -1;
    }
    if (unlink(names.ready_file) != 0 && errno != ENOENT) {
        set_error(error, error_len, "cannot remove runtime readiness: %s",
                  strerror(errno));
        return -1;
    }
    return 0;
}
