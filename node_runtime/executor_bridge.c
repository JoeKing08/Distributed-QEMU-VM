#define _GNU_SOURCE

#include "executor_bridge.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "../common_include/wavevm_executor_abi.h"
#include "../common_include/wavevm_protocol.h"

struct bridge_state {
    struct wvm_executor_bridge_config config;
    char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
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

int wvm_executor_bridge_v1_compat_dispatch(
    void *opaque, const struct wvm_envelope_v1 *envelope, char *error,
    size_t error_len)
{
    (void)opaque;

    if (!envelope) {
        set_error(error, error_len, "missing V1 envelope for executor adapter");
        return -EINVAL;
    }
    /*
     * Do not cast envelope->payload to struct wvm_header here. That would
     * silently treat an incomplete V1 semantic schema as legacy traffic and
     * bypass the node-runtime/executor ABI migration boundary.
     */
    set_error(error, error_len,
              "V1 message type %u has no typed executor ABI adapter",
              (unsigned)envelope->message_type);
    return -ENOTSUP;
}

static int send_result(int fd, const struct wvm_executor_abi_frame *request,
                       uint16_t status)
{
    struct wvm_executor_abi_frame result;
    uint8_t encoded[WVM_EXECUTOR_ABI_HEADER_BYTES];
    size_t encoded_bytes;
    char error[128] = {0};

    memset(&result, 0, sizeof(result));
    result.identity = request->identity;
    result.message_type = WVM_EXECUTOR_ABI_RESULT;
    result.status = status;
    if (wvm_executor_abi_encode(&result, encoded, sizeof(encoded),
                                &encoded_bytes, error, sizeof(error)) != 0) {
        return -1;
    }
    return send(fd, encoded, encoded_bytes, MSG_NOSIGNAL) ==
                   (ssize_t)encoded_bytes
               ? 0
               : -1;
}

static int forward_cpu_run(const struct bridge_state *state,
                           const struct wvm_executor_abi_frame *request)
{
    const struct wvm_header *header;
    struct sockaddr_in executor_addr;
    struct sockaddr_in response_addr;
    struct pollfd pollfd;
    uint8_t response[WVM_MAX_PACKET_SIZE];
    socklen_t response_len = sizeof(response_addr);
    ssize_t sent;
    ssize_t received;
    int sock;

    if (!state || !request || request->payload_bytes < sizeof(*header)) {
        return -EINVAL;
    }
    header = (const struct wvm_header *)request->payload;
    if (ntohl(header->magic) != WVM_MAGIC ||
        ntohs(header->msg_type) != MSG_VCPU_RUN ||
        ntohs(header->payload_len) !=
            request->payload_bytes - sizeof(struct wvm_header)) {
        return -EPROTO;
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -errno;
    }
    memset(&executor_addr, 0, sizeof(executor_addr));
    executor_addr.sin_family = AF_INET;
    executor_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    executor_addr.sin_port = htons(state->config.executor_service_port);
    sent = sendto(sock, request->payload, request->payload_bytes, MSG_NOSIGNAL,
                  (struct sockaddr *)&executor_addr, sizeof(executor_addr));
    if (sent != (ssize_t)request->payload_bytes) {
        int result = errno ? -errno : -EIO;
        close(sock);
        return result;
    }

    pollfd.fd = sock;
    pollfd.events = POLLIN;
    /*
     * This is a transport bridge, not a vCPU semantic timeout.  The bridge
     * waits for the executor result; the executor owns the execution
     * interval and the caller owns any higher-level lifecycle deadline.
     */
    if (poll(&pollfd, 1, -1) <= 0) {
        int result = errno ? -errno : -EIO;
        close(sock);
        return result;
    }
    received = recvfrom(sock, response, sizeof(response), 0,
                        (struct sockaddr *)&response_addr, &response_len);
    close(sock);
    if (received < (ssize_t)sizeof(struct wvm_header)) {
        return -EPROTO;
    }
    header = (const struct wvm_header *)response;
    if (ntohl(header->magic) != WVM_MAGIC ||
        ntohs(header->msg_type) != MSG_VCPU_EXIT ||
        ntohs(header->payload_len) !=
            (size_t)received - sizeof(struct wvm_header)) {
        return -EPROTO;
    }

    memset(&response_addr, 0, sizeof(response_addr));
    response_addr.sin_family = AF_INET;
    response_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    response_addr.sin_port = htons(state->config.node_runtime_port);
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -errno;
    }
    sent = sendto(sock, response, (size_t)received, MSG_NOSIGNAL,
                  (struct sockaddr *)&response_addr, sizeof(response_addr));
    close(sock);
    if (sent != received) {
        return errno ? -errno : -EIO;
    }
    return 0;
}

static int authorize_cpu_run(const struct bridge_state *state,
                             const struct wvm_executor_abi_frame *request,
                             char *error, size_t error_len)
{
    struct wvm_runtime_operation operation;

    if (!state || !request) {
        return -EINVAL;
    }
    memset(&operation, 0, sizeof(operation));
    operation.connection_id = state->config.runtime_connection_id;
    operation.vm_id = request->identity.vm_id;
    operation.vm_incarnation = request->identity.vm_incarnation;
    operation.manifest_generation = request->identity.manifest_generation;
    memcpy(operation.candidate_manifest_digest,
           request->identity.candidate_manifest_digest,
           sizeof(operation.candidate_manifest_digest));
    operation.route_snapshot_key = request->identity.route_snapshot_key;
    memcpy(operation.activation_fence, request->identity.activation_fence,
           sizeof(operation.activation_fence));
    memcpy(operation.operation_id, request->identity.operation_id,
           sizeof(operation.operation_id));
    return wvm_runtime_gate_authorize(state->config.runtime_gate, &operation,
                                      error, error_len);
}

static void *executor_bridge_thread(void *opaque)
{
    struct bridge_state *state = opaque;
    struct sockaddr_un address;
    int server_fd;

    unlink(state->socket_path);
    server_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (server_fd < 0) {
        perror("[executor-abi] socket");
        return NULL;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s",
             state->socket_path);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        chmod(state->socket_path, 0600) != 0 ||
        listen(server_fd, 16) != 0) {
        perror("[executor-abi] bind/listen");
        close(server_fd);
        unlink(state->socket_path);
        return NULL;
    }
    fprintf(stderr,
            "[executor-abi] listening socket=%s executor_port=%u node_port=%u\n",
            state->socket_path, (unsigned)state->config.executor_service_port,
            (unsigned)state->config.node_runtime_port);

    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL);
        uint8_t *frame_bytes;
        struct wvm_executor_abi_frame request;
        ssize_t received;
        char error[256] = {0};
        size_t capacity = WVM_EXECUTOR_ABI_HEADER_BYTES + WVM_MAX_PACKET_SIZE;
        int decoded = 0;

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        frame_bytes = malloc(capacity);
        if (!frame_bytes) {
            close(client_fd);
            continue;
        }
        received = recv(client_fd, frame_bytes, capacity, 0);
        if (received > 0 &&
            wvm_executor_abi_decode(frame_bytes, (size_t)received, &request,
                                    error, sizeof(error)) == 0) {
            decoded = 1;
        }
        if (!decoded ||
            wvm_executor_abi_validate_identity(
                &request, state->config.manifest,
                state->config.local_runtime_instance_id, error,
                sizeof(error)) != 0) {
            if (decoded) {
                (void)send_result(client_fd, &request,
                                  WVM_EXECUTOR_ABI_STALE_IDENTITY);
            }
            free(frame_bytes);
            close(client_fd);
            continue;
        }

        if (request.message_type != WVM_EXECUTOR_ABI_CPU_RUN) {
            (void)send_result(client_fd, &request,
                              WVM_EXECUTOR_ABI_UNSUPPORTED);
        } else if (authorize_cpu_run(state, &request, error,
                                     sizeof(error)) != 0) {
            (void)send_result(client_fd, &request,
                              WVM_EXECUTOR_ABI_STALE_IDENTITY);
        } else {
            int result = forward_cpu_run(state, &request);
            (void)send_result(
                client_fd, &request,
                result == 0 ? WVM_EXECUTOR_ABI_SUCCESS
                            : WVM_EXECUTOR_ABI_INTERNAL_FAILURE);
        }
        free(frame_bytes);
        close(client_fd);
    }
    close(server_fd);
    unlink(state->socket_path);
    free(state);
    return NULL;
}

int wvm_executor_bridge_start(
    const struct wvm_executor_bridge_config *config, pthread_t *thread_out)
{
    struct bridge_state *state;
    pthread_t thread;

    if (!config || !config->manifest || !config->runtime_gate ||
        config->runtime_connection_id == 0 || !config->socket_path ||
        config->socket_path[0] == '\0' ||
        strlen(config->socket_path) >= sizeof(state->socket_path) ||
        config->executor_service_port == 0 ||
        config->node_runtime_port == 0 ||
        config->local_runtime_instance_id == 0) {
        return -EINVAL;
    }
    state = calloc(1, sizeof(*state));
    if (!state) {
        return -ENOMEM;
    }
    state->config = *config;
    snprintf(state->socket_path, sizeof(state->socket_path), "%s",
             config->socket_path);
    if (pthread_create(&thread, NULL, executor_bridge_thread, state) != 0) {
        free(state);
        return -errno;
    }
    pthread_detach(thread);
    if (thread_out) {
        *thread_out = thread;
    }
    return 0;
}
