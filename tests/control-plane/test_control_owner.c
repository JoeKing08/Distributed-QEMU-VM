#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "wavevm_control_owner.h"

struct callback_context {
    struct wvm_member_key actor;
    atomic_uint authenticate_calls;
    atomic_uint apply_calls;
};

struct client_context {
    const char *socket_path;
    uint8_t operation_id[WVM_IDENTITY_ID_BYTES];
    int result;
};

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "control-owner test: %s\n", message);
        return -1;
    }
    return 0;
}

static void write_be32(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static uint32_t read_be32(const uint8_t bytes[4])
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static int write_full(int fd, const uint8_t *bytes, size_t count)
{
    size_t offset = 0;

    while (offset < count) {
        ssize_t written = write(fd, bytes + offset, count - offset);

        if (written > 0) {
            offset += (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

static int read_full(int fd, uint8_t *bytes, size_t count)
{
    size_t offset = 0;

    while (offset < count) {
        ssize_t received = read(fd, bytes + offset, count - offset);

        if (received > 0) {
            offset += (size_t)received;
        } else if (received < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

static int authenticate_actor(void *opaque, int stream_fd,
                              struct wvm_member_key *actor, char *error,
                              size_t error_len)
{
    struct callback_context *context = opaque;

    (void)stream_fd;
    (void)error;
    (void)error_len;
    if (!context || !actor) {
        return -1;
    }
    *actor = context->actor;
    atomic_fetch_add_explicit(&context->authenticate_calls, 1,
                              memory_order_relaxed);
    return 0;
}

static int apply_request(
    void *opaque, const struct wvm_envelope *request,
    const struct wvm_member_key *authenticated_actor,
    struct wvm_membership_control_result *result, char *error,
    size_t error_len)
{
    struct callback_context *context = opaque;

    (void)error;
    (void)error_len;
    if (!context || !request || !authenticated_actor || !result ||
        authenticated_actor->role_type != context->actor.role_type ||
        authenticated_actor->role_id != context->actor.role_id ||
        authenticated_actor->instance_id != context->actor.instance_id) {
        return -1;
    }
    memset(result, 0, sizeof(*result));
    result->status_code = WVM_MEMBERSHIP_CONTROL_SUCCESS;
    memcpy(result->in_reply_to_operation_id, request->operation_id,
           sizeof(result->in_reply_to_operation_id));
    atomic_fetch_add_explicit(&context->apply_calls, 1, memory_order_relaxed);
    return 0;
}

static int connect_owner(const char *socket_path)
{
    struct sockaddr_un address;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path);
    if (connect(fd, (const struct sockaddr *)&address,
                (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
                            strlen(socket_path) + 1U)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int exchange(const char *socket_path,
                    const uint8_t operation_id[WVM_IDENTITY_ID_BYTES])
{
    struct wvm_envelope request;
    struct wvm_envelope response;
    struct wvm_membership_control_result result;
    uint8_t prefix[4];
    uint8_t response_prefix[4];
    uint8_t frame[WVM_CONTROL_TRANSPORT_DEFAULT_MAX_FRAME_BYTES];
    uint8_t response_frame[WVM_ENVELOPE_HEADER_BYTES +
                           WVM_MEMBERSHIP_CONTROL_RESULT_BYTES];
    size_t frame_bytes = 0;
    uint32_t response_bytes;
    char error[256] = {0};
    int fd;
    int status = -1;

    fd = connect_owner(socket_path);
    if (fd < 0) {
        return -1;
    }
    memset(&request, 0, sizeof(request));
    request.message_type = WVM_ENVELOPE_MSG_REGISTER_MEMBER;
    request.origin_physical_node_id = 17;
    request.origin_runtime_instance_id = 19;
    memcpy(request.operation_id, operation_id, sizeof(request.operation_id));
    request.delivery_attempt_id = 1;
    if (wvm_envelope_encode(&request, WVM_ENVELOPE_TRANSPORT_LOCAL, frame,
                            sizeof(frame), &frame_bytes, error,
                            sizeof(error)) != 0 ||
        frame_bytes > UINT32_MAX) {
        goto cleanup;
    }
    write_be32(prefix, (uint32_t)frame_bytes);
    if (write_full(fd, prefix, sizeof(prefix)) != 0 ||
        write_full(fd, frame, frame_bytes) != 0 ||
        read_full(fd, response_prefix, sizeof(response_prefix)) != 0) {
        goto cleanup;
    }
    response_bytes = read_be32(response_prefix);
    if (response_bytes < WVM_ENVELOPE_HEADER_BYTES ||
        response_bytes > sizeof(response_frame) ||
        read_full(fd, response_frame, response_bytes) != 0 ||
        wvm_envelope_decode(response_frame, response_bytes,
                            WVM_ENVELOPE_TRANSPORT_LOCAL, &response, error,
                            sizeof(error)) != 0 ||
        response.message_type != WVM_ENVELOPE_MSG_CTRL_RESULT ||
        response.payload_bytes != WVM_MEMBERSHIP_CONTROL_RESULT_BYTES ||
        wvm_membership_control_result_decode(response.payload, &result) != 0 ||
        result.status_code != WVM_MEMBERSHIP_CONTROL_SUCCESS ||
        memcmp(result.in_reply_to_operation_id, operation_id,
               sizeof(result.in_reply_to_operation_id)) != 0) {
        goto cleanup;
    }
    status = 0;

cleanup:
    shutdown(fd, SHUT_RDWR);
    close(fd);
    return status;
}

static void *client_main(void *opaque)
{
    struct client_context *context = opaque;

    context->result = exchange(context->socket_path, context->operation_id);
    return NULL;
}

int main(void)
{
    char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    struct wvm_control_owner_config config;
    struct wvm_control_owner owner;
    struct callback_context callbacks;
    struct client_context clients[2];
    pthread_t threads[2];
    char error[256] = {0};
    struct stat socket_stat;
    size_t i;
    int result = 1;

    snprintf(socket_path, sizeof(socket_path), "/tmp/wavevm-owner-%ld.sock",
             (long)getpid());
    unlink(socket_path);
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.actor.role_type = WVM_MANIFEST_ROLE_NODE_RUNTIME;
    callbacks.actor.role_id = 17;
    callbacks.actor.instance_id = 19;
    memset(&config, 0, sizeof(config));
    config.socket_path = socket_path;
    config.socket_mode = S_IRUSR | S_IWUSR;
    config.listen_backlog = 8;
    config.local_physical_node_id = 900;
    config.local_runtime_instance_id = 901;
    config.authenticate = authenticate_actor;
    config.authenticate_opaque = &callbacks;
    config.apply = apply_request;
    config.apply_opaque = &callbacks;

    if (expect(wvm_control_owner_init(&owner, &config, error,
                                      sizeof(error)) == 0,
               "initialize control owner") != 0 ||
        expect(wvm_control_owner_start(&owner, error, sizeof(error)) == 0,
               "start control owner") != 0 ||
        expect(stat(socket_path, &socket_stat) == 0 &&
                   (socket_stat.st_mode & 0777) == (S_IRUSR | S_IWUSR),
               "publish protected Unix listener") != 0) {
        goto cleanup_owner;
    }
    memset(clients, 0, sizeof(clients));
    for (i = 0; i < 2; i++) {
        clients[i].socket_path = socket_path;
        clients[i].operation_id[WVM_IDENTITY_ID_BYTES - 1] =
            (uint8_t)(i + 1U);
        if (expect(pthread_create(&threads[i], NULL, client_main,
                                  &clients[i]) == 0,
                   "start concurrent control client") != 0) {
            goto stop_owner;
        }
    }
    for (i = 0; i < 2; i++) {
        pthread_join(threads[i], NULL);
    }
    if (expect(clients[0].result == 0 && clients[1].result == 0,
               "serve concurrent typed control results") != 0 ||
        expect(atomic_load_explicit(&callbacks.authenticate_calls,
                                    memory_order_relaxed) == 2,
               "authenticate each stream independently") != 0 ||
        expect(atomic_load_explicit(&callbacks.apply_calls,
                                    memory_order_relaxed) == 2,
               "apply each stream independently") != 0) {
        goto stop_owner;
    }
    result = 0;

stop_owner:
    if (expect(wvm_control_owner_stop(&owner, error, sizeof(error)) == 0,
               "stop and join control owner clients") != 0) {
        result = 1;
    }
    if (expect(stat(socket_path, &socket_stat) != 0 && errno == ENOENT,
               "remove listener after owner stop") != 0) {
        result = 1;
    }

cleanup_owner:
    wvm_control_owner_destroy(&owner);
    unlink(socket_path);
    if (result != 0 && error[0] != '\0') {
        fprintf(stderr, "control-owner test: %s\n", error);
    }
    return result;
}
