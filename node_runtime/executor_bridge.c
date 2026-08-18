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
#include <time.h>
#include <unistd.h>

#include "../common_include/wavevm_executor_abi.h"
#include "kvm_page_cache.h"
#include "../common_include/wavevm_protocol.h"
#include "../common_include/wavevm_vcpu_handoff_cache.h"
#include "../common_include/wavevm_vcpu_handoff.h"
#include "../common_include/wavevm_x86_context.h"

struct bridge_state {
    struct wvm_executor_bridge_config config;
    struct wvm_vcpu_handoff_cache handoff_cache;
    char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    pthread_mutex_t task_lock;
    pthread_cond_t task_available;
    pthread_t task_worker;
    pthread_t server_thread;
    struct bridge_task *tasks;
    size_t task_capacity;
    size_t task_head;
    size_t task_count;
    uint64_t next_legacy_request_id;
    uint64_t next_completed_memory_fence_id;
    int server_fd;
    int stopping;
    int task_worker_started;
    int server_thread_started;
};

struct bridge_task {
    struct wvm_envelope envelope;
    uint8_t *payload;
};

static int authorize_cpu_run(const struct bridge_state *state,
                             const struct wvm_executor_abi_frame *request,
                             char *error, size_t error_len);

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

static uint64_t monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    if ((uint64_t)now.tv_sec > UINT64_MAX / 1000U) {
        return UINT64_MAX;
    }
    return (uint64_t)now.tv_sec * 1000U +
           (uint64_t)now.tv_nsec / 1000000U;
}

static int route_key_equal(const struct wvm_route_snapshot_key *left,
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

static int route_destination_equal(
    const struct wvm_runtime_route_destination *destination,
    const struct wvm_envelope_route *route)
{
    return destination && route &&
           destination->destination_kind == route->destination_kind &&
           destination->destination_scope == route->destination_scope &&
           destination->destination_vnode ==
               route->destination_vnode_or_endpoint;
}

static int config_is_admitted(const struct wvm_executor_bridge_config *config,
                              char *error, size_t error_len)
{
    if (!config || !config->manifest || !config->runtime_gate ||
        !config->dispatch || !config->route_runtime ||
        !config->send_envelope ||
        config->operation_retention_horizon_ms == 0 ||
        config->runtime_gate->state != WVM_RUNTIME_GATE_ACTIVE ||
        config->runtime_gate->manifest != config->manifest ||
        config->local_runtime_instance_id == 0 ||
        config->runtime_connection_id == 0 ||
        config->manifest->launch_plan.vcpu_handoff_record_capacity == 0 ||
        config->dispatch->vm_id != config->manifest->vm_id ||
        config->dispatch->vm_incarnation != config->manifest->vm_incarnation ||
        config->dispatch->manifest_generation !=
            config->manifest->manifest_generation ||
        config->dispatch->physical_node_id != config->manifest->physical_node_id ||
        config->dispatch->expected_node_instance_id !=
            config->local_runtime_instance_id ||
        memcmp(config->dispatch->candidate_manifest_digest,
               config->manifest->candidate_manifest_digest,
               sizeof(config->dispatch->candidate_manifest_digest)) != 0 ||
        memcmp(config->dispatch->activation_fence,
               config->manifest->activation_fence,
               sizeof(config->dispatch->activation_fence)) != 0 ||
        !route_key_equal(&config->dispatch->required_route_snapshot_key,
                         &config->manifest->required_route_snapshot_key) ||
        wvm_runtime_dispatch_projection_validate(config->dispatch, error,
                                                 error_len) != 0 ||
        !wvm_route_runtime_has_snapshot(
            config->route_runtime,
            &config->dispatch->required_route_snapshot_key)) {
        if (!error || error[0] == '\0') {
            set_error(error, error_len,
                      "executor bridge configuration is not admitted");
        }
        return -EINVAL;
    }
    return 0;
}

static int authorize_local_handoff(
    const struct bridge_state *state,
    const struct wvm_vcpu_handoff_request *request,
    const struct wvm_envelope *envelope, char *error, size_t error_len)
{
    if (!state || !request || !envelope ||
        !route_destination_equal(&state->config.dispatch->local_primary,
                                 &envelope->route) ||
        request->destination_executor_id !=
            state->config.dispatch->local_primary.destination_vnode) {
        set_error(error, error_len,
                  "typed vCPU handoff does not target this admitted executor");
        return -EPERM;
    }
    return 0;
}

static void handoff_result_from_request(
    const struct wvm_vcpu_handoff_request *request, uint16_t status,
    struct wvm_vcpu_handoff_result *result)
{
    memset(result, 0, sizeof(*result));
    result->protocol_version = WVM_VCPU_HANDOFF_RESULT_VERSION;
    result->status = status;
    result->exit_class =
        status == WVM_VCPU_HANDOFF_RESULT_EXECUTOR_FAILURE
            ? WVM_VCPU_EXIT_EXECUTOR_ERROR
            : status == WVM_VCPU_HANDOFF_RESULT_MEMORY_FAILURE
                  ? WVM_VCPU_EXIT_MEMORY_ERROR
                  : WVM_VCPU_EXIT_NONE;
    result->backend = request->backend;
    result->vm_id = request->vm_id;
    result->vm_incarnation = request->vm_incarnation;
    result->manifest_generation = request->manifest_generation;
    result->origin_physical_node_id = request->origin_physical_node_id;
    result->origin_runtime_instance_id = request->origin_runtime_instance_id;
    result->vcpu_index = request->vcpu_index;
    result->handoff_sequence = request->handoff_sequence;
    memcpy(result->operation_id, request->operation_id,
           sizeof(result->operation_id));
    result->context_schema_version = request->context_schema_version;
}

static int resolve_reply_route(
    const struct bridge_state *state,
    const struct wvm_vcpu_handoff_request *request,
    struct wvm_envelope_route *route, char *error, size_t error_len)
{
    struct wvm_route_runtime_next_hop next_hop;

    if (!state || !request || !route ||
        wvm_route_runtime_lookup_destination(
            state->config.route_runtime,
            &state->config.dispatch->required_route_snapshot_key,
            request->reply_destination_kind, request->reply_destination_scope,
            request->reply_destination_vnode, &next_hop, error,
            error_len) != 0) {
        return -ESTALE;
    }
    memset(route, 0, sizeof(*route));
    route->destination_kind = request->reply_destination_kind;
    route->destination_scope = request->reply_destination_scope;
    route->destination_vnode_or_endpoint = request->reply_destination_vnode;
    route->hop_limit = next_hop.hop_limit;
    return 0;
}

static int send_typed_result(
    const struct bridge_state *state, const struct wvm_envelope *request_envelope,
    const struct wvm_vcpu_handoff_request *request,
    const struct wvm_vcpu_handoff_result *result, char *error,
    size_t error_len)
{
    struct wvm_envelope_route reply_route;
    struct wvm_envelope response;
    uint8_t *payload;
    size_t payload_bytes = 0;
    int send_result;

    if (!state || !request_envelope || !request || !result ||
        request_envelope->delivery_attempt_id == UINT64_MAX) {
        set_error(error, error_len,
                  "typed vCPU response delivery attempt is invalid");
        return -ERANGE;
    }
    if (resolve_reply_route(state, request, &reply_route, error, error_len) !=
        0) {
        return -ESTALE;
    }
    payload = malloc(WVM_ENVELOPE_MAX_NETWORK_LOGICAL_PAYLOAD);
    if (!payload) {
        set_error(error, error_len, "cannot allocate typed vCPU exit payload");
        return -ENOMEM;
    }
    if (wvm_vcpu_handoff_exit_envelope_build(
            request_envelope, request, &reply_route,
            request_envelope->delivery_attempt_id + 1U, result, payload,
            WVM_ENVELOPE_MAX_NETWORK_LOGICAL_PAYLOAD, &payload_bytes, &response,
            error, error_len) != 0) {
        free(payload);
        return -EINVAL;
    }
    send_result = state->config.send_envelope(
        state->config.send_envelope_opaque, &response, error, error_len);
    free(payload);
    return send_result;
}

static int cache_terminal_and_send(
    struct bridge_state *state, const struct wvm_envelope *envelope,
    const struct wvm_vcpu_handoff_request *request,
    const struct wvm_vcpu_handoff_result *result, char *error,
    size_t error_len)
{
    if (wvm_vcpu_handoff_cache_complete(
            &state->handoff_cache, request, envelope, result,
            monotonic_milliseconds(), error, error_len) != 0) {
        return -EIO;
    }
    return send_typed_result(state, envelope, request, result, error,
                             error_len);
}

static int bridge_stopping(struct bridge_state *state)
{
    int stopping;

    pthread_mutex_lock(&state->task_lock);
    stopping = state->stopping;
    pthread_mutex_unlock(&state->task_lock);
    return stopping;
}

static uint64_t next_nonzero_id(uint64_t *counter)
{
    uint64_t value;

    value = ++*counter;
    if (value == 0) {
        value = ++*counter;
    }
    return value;
}

static int legacy_executor_id(const struct bridge_state *state,
                              uint32_t *identifier)
{
    const struct wvm_node_runtime_manifest *manifest;
    uint32_t vnode;

    if (!state || !identifier) {
        return -EINVAL;
    }
    manifest = state->config.manifest;
    vnode = state->config.dispatch->local_primary.destination_vnode;
    if (manifest->vm_id > UINT8_MAX || vnode > WVM_NODEID_MASK) {
        return -ERANGE;
    }
    *identifier = WVM_ENCODE_ID(manifest->vm_id, vnode);
    return 0;
}

static uint16_t exit_class_from_tcg_ack(const wvm_tcg_context_t *context)
{
    if (context->halted) {
        return WVM_VCPU_EXIT_HALTED;
    }
    if (context->interrupt_request != 0) {
        return WVM_VCPU_EXIT_INTERRUPT;
    }
    /*
     * The TCG helper ends a remote slice with its host-side preemption kick
     * unless the guest reports a more specific terminal condition above.
     */
    return WVM_VCPU_EXIT_BUDGET;
}

static int build_legacy_cpu_run(
    struct bridge_state *state,
    const struct wvm_vcpu_handoff_request *request,
    struct wvm_header *header, struct wvm_ipc_cpu_run_req *legacy,
    uint64_t *request_id, char *error, size_t error_len)
{
    uint32_t local_id;
    size_t legacy_bytes;
    void *legacy_context;
    uint64_t context_fields = 0;

    if (!state || !request || !header || !legacy || !request_id ||
        (request->backend != WVM_VCPU_BACKEND_TCG &&
         request->backend != WVM_VCPU_BACKEND_KVM) ||
        legacy_executor_id(state, &local_id) != 0) {
        set_error(error, error_len,
                  "typed vCPU handoff cannot address the legacy executor");
        return -EINVAL;
    }
    memset(legacy, 0, sizeof(*legacy));
    legacy->mode_tcg = request->backend == WVM_VCPU_BACKEND_TCG;
    legacy->slave_id = WVM_NODE_AUTO_ROUTE;
    legacy->vcpu_index = request->vcpu_index;
    legacy_bytes = wvm_x86_context_legacy_bytes(request->backend);
    legacy_context = request->backend == WVM_VCPU_BACKEND_TCG
                         ? (void *)&legacy->ctx.tcg
                         : (void *)&legacy->ctx.kvm;
    if (legacy_bytes == 0 ||
        wvm_x86_context_decode(
            request->backend, request->context, request->context_bytes,
            &context_fields, legacy_context, legacy_bytes, error,
            error_len) != 0 ||
        context_fields != request->context_valid_fields) {
        if (!error || error[0] == '\0') {
            set_error(error, error_len,
                      "typed vCPU context cannot populate legacy executor ABI");
        }
        return -EPROTO;
    }

    *request_id = next_nonzero_id(&state->next_legacy_request_id);
    memset(header, 0, sizeof(*header));
    header->magic = htonl(WVM_MAGIC);
    header->msg_type = htons(MSG_VCPU_RUN);
    header->payload_len = htons(sizeof(*legacy));
    header->slave_id = htonl(local_id);
    header->target_id = htonl(local_id);
    header->req_id = WVM_HTONLL(*request_id);
    header->mode_tcg = 1;
    return 0;
}

static int receive_legacy_cpu_exit(
    struct bridge_state *state, const struct wvm_header *request_header,
    const struct wvm_ipc_cpu_run_req *legacy_request,
    struct wvm_ipc_cpu_run_ack *ack, char *error, size_t error_len)
{
    struct sockaddr_in executor_address;
    struct pollfd pollfd;
    uint8_t request_packet[sizeof(*request_header) + sizeof(*legacy_request)];
    uint8_t response[sizeof(struct wvm_header) +
                     sizeof(struct wvm_ipc_cpu_run_ack)];
    ssize_t sent;
    int fd;

    if (!state || !request_header || !legacy_request || !ack ||
        state->config.executor_service_port == 0) {
        set_error(error, error_len, "legacy executor endpoint is unavailable");
        return -ENOTCONN;
    }
    fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        set_error(error, error_len, "cannot open local executor socket: %s",
                  strerror(errno));
        return -errno;
    }
    memset(&executor_address, 0, sizeof(executor_address));
    executor_address.sin_family = AF_INET;
    executor_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    executor_address.sin_port = htons(state->config.executor_service_port);
    if (connect(fd, (const struct sockaddr *)&executor_address,
                sizeof(executor_address)) != 0) {
        int result = -errno;

        close(fd);
        set_error(error, error_len, "cannot connect local executor socket");
        return result;
    }
    memcpy(request_packet, request_header, sizeof(*request_header));
    memcpy(request_packet + sizeof(*request_header), legacy_request,
           sizeof(*legacy_request));
    sent = send(fd, request_packet, sizeof(request_packet), MSG_NOSIGNAL);
    if (sent != (ssize_t)sizeof(request_packet)) {
        int result = sent < 0 ? -errno : -EIO;

        close(fd);
        set_error(error, error_len, "cannot submit local executor request");
        return result;
    }

    /*
     * This is intentionally not an execution timeout. It only gives shutdown
     * a chance to stop the worker; the executor itself owns slice duration.
     */
    pollfd.fd = fd;
    pollfd.events = POLLIN;
    for (;;) {
        ssize_t received;
        const struct wvm_header *response_header;

        if (bridge_stopping(state)) {
            close(fd);
            return -ESHUTDOWN;
        }
        if (poll(&pollfd, 1, 100) < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return -errno;
        }
        if ((pollfd.revents & POLLIN) == 0) {
            continue;
        }
        received = recv(fd, response, sizeof(response), 0);
        if (received != (ssize_t)sizeof(response)) {
            continue;
        }
        response_header = (const struct wvm_header *)response;
        if (ntohl(response_header->magic) != WVM_MAGIC ||
            ntohs(response_header->msg_type) != MSG_VCPU_EXIT ||
            ntohs(response_header->payload_len) != sizeof(*ack) ||
            WVM_NTOHLL(response_header->req_id) !=
                WVM_NTOHLL(request_header->req_id) ||
            response_header->slave_id != request_header->target_id ||
            response_header->target_id != request_header->slave_id) {
            continue;
        }
        memcpy(ack, response + sizeof(*response_header), sizeof(*ack));
        close(fd);
        if (ack->mode_tcg != legacy_request->mode_tcg) {
            set_error(error, error_len,
                      "legacy executor exit returned another backend");
            return -EPROTO;
        }
        return 0;
    }
}

static uint16_t exit_class_from_legacy_ack(
    const struct wvm_vcpu_handoff_request *request,
    const struct wvm_ipc_cpu_run_ack *ack)
{
    uint32_t exit_reason;

    if (request->backend == WVM_VCPU_BACKEND_TCG) {
        return exit_class_from_tcg_ack(&ack->ctx.tcg);
    }
    exit_reason = ack->ctx.kvm.exit_reason;
    if (exit_reason == KVM_EXIT_HLT) {
        return WVM_VCPU_EXIT_HALTED;
    }
    if (exit_reason == KVM_EXIT_IO) {
        return WVM_VCPU_EXIT_PIO;
    }
    if (exit_reason == KVM_EXIT_MMIO) {
        return WVM_VCPU_EXIT_MMIO;
    }
    if (exit_reason == WVM_EXIT_PREEMPT) {
        return WVM_VCPU_EXIT_BUDGET;
    }
    return WVM_VCPU_EXIT_BUDGET;
}

static int execute_typed_task(struct bridge_state *state,
                              const struct bridge_task *task, char *error,
                              size_t error_len)
{
    struct wvm_vcpu_handoff_request request;
    struct wvm_vcpu_handoff_result result;
    struct wvm_header legacy_header;
    struct wvm_ipc_cpu_run_req legacy_request;
    struct wvm_ipc_cpu_run_ack legacy_ack;
    uint8_t encoded_context[WVM_X86_CONTEXT_WIRE_HEADER_BYTES +
                            sizeof(wvm_tcg_context_t)];
    uint64_t request_id;
    size_t encoded_context_bytes = 0;

    if (!state || !task ||
        wvm_vcpu_handoff_request_decode(
            task->envelope.payload, task->envelope.payload_bytes, &request,
            error, error_len) != 0) {
        return -EINVAL;
    }

    if (request.backend == WVM_VCPU_BACKEND_KVM &&
        !wvm_kvm_page_cache_global_active()) {
        handoff_result_from_request(
            &request, WVM_VCPU_HANDOFF_RESULT_MEMORY_FAILURE, &result);
        return cache_terminal_and_send(state, &task->envelope, &request,
                                       &result, error, error_len);
    }

    if (build_legacy_cpu_run(state, &request, &legacy_header, &legacy_request,
                             &request_id, error, error_len) != 0 ||
        receive_legacy_cpu_exit(state, &legacy_header, &legacy_request,
                                 &legacy_ack, error, error_len) != 0) {
        handoff_result_from_request(
            &request, WVM_VCPU_HANDOFF_RESULT_EXECUTOR_FAILURE, &result);
        return cache_terminal_and_send(state, &task->envelope, &request,
                                       &result, error, error_len);
    }
    if (legacy_ack.status == WVM_CPU_RUN_STATUS_MEMORY_FAILURE) {
        handoff_result_from_request(
            &request, WVM_VCPU_HANDOFF_RESULT_MEMORY_FAILURE, &result);
        return cache_terminal_and_send(state, &task->envelope, &request,
                                       &result, error, error_len);
    }
    if (legacy_ack.status != 0 ||
        wvm_x86_context_encode(
            request.backend, request.context_valid_fields,
            request.backend == WVM_VCPU_BACKEND_TCG
                ? (const void *)&legacy_ack.ctx.tcg
                : (const void *)&legacy_ack.ctx.kvm,
            wvm_x86_context_legacy_bytes(request.backend), encoded_context,
            sizeof(encoded_context), &encoded_context_bytes, error,
            error_len) != 0) {
        handoff_result_from_request(
            &request, WVM_VCPU_HANDOFF_RESULT_EXECUTOR_FAILURE, &result);
        return cache_terminal_and_send(state, &task->envelope, &request,
                                       &result, error, error_len);
    }

    handoff_result_from_request(&request, WVM_VCPU_HANDOFF_RESULT_SUCCESS,
                                &result);
    result.exit_class = exit_class_from_legacy_ack(&request, &legacy_ack);
    result.produced_memory_fence_id =
        next_nonzero_id(&state->next_completed_memory_fence_id);
    result.context_valid_fields = request.context_valid_fields;
    result.context = encoded_context;
    result.context_bytes = encoded_context_bytes;
    return cache_terminal_and_send(state, &task->envelope, &request, &result,
                                   error, error_len);
}

static void task_release(struct bridge_task *task)
{
    if (!task) {
        return;
    }
    free(task->payload);
    memset(task, 0, sizeof(*task));
}

static void *typed_task_worker(void *opaque)
{
    struct bridge_state *state = opaque;

    for (;;) {
        struct bridge_task task;
        char error[256] = {0};

        memset(&task, 0, sizeof(task));
        pthread_mutex_lock(&state->task_lock);
        while (!state->stopping && state->task_count == 0) {
            pthread_cond_wait(&state->task_available, &state->task_lock);
        }
        if (state->stopping) {
            pthread_mutex_unlock(&state->task_lock);
            break;
        }
        task = state->tasks[state->task_head];
        memset(&state->tasks[state->task_head], 0,
               sizeof(state->tasks[state->task_head]));
        state->task_head = (state->task_head + 1U) % state->task_capacity;
        state->task_count--;
        pthread_mutex_unlock(&state->task_lock);

        (void)execute_typed_task(state, &task, error, sizeof(error));
        task_release(&task);
    }
    return NULL;
}

static int enqueue_typed_task(struct bridge_state *state,
                              const struct wvm_envelope *envelope)
{
    struct bridge_task task;
    size_t tail;

    memset(&task, 0, sizeof(task));
    task.payload = malloc(envelope->payload_bytes);
    if (!task.payload) {
        return -ENOMEM;
    }
    memcpy(task.payload, envelope->payload, envelope->payload_bytes);
    task.envelope = *envelope;
    task.envelope.payload = task.payload;

    pthread_mutex_lock(&state->task_lock);
    if (state->stopping || state->task_count == state->task_capacity) {
        pthread_mutex_unlock(&state->task_lock);
        task_release(&task);
        return state->stopping ? -ESHUTDOWN : -ENOBUFS;
    }
    tail = (state->task_head + state->task_count) % state->task_capacity;
    state->tasks[tail] = task;
    state->task_count++;
    pthread_cond_signal(&state->task_available);
    pthread_mutex_unlock(&state->task_lock);
    return 0;
}

int wvm_executor_bridge_dispatch(
    void *opaque, const struct wvm_envelope *envelope, char *error,
    size_t error_len)
{
    struct bridge_state *state = opaque;
    struct wvm_vcpu_handoff_request request;
    struct wvm_executor_abi_frame operation;
    struct wvm_vcpu_handoff_result result;
    enum wvm_vcpu_handoff_cache_decision cache_decision;
    uint8_t *replay = NULL;
    uint64_t encoded_context_fields = 0;
    size_t replay_bytes = 0;
    int cache_result;

    if (!state || !envelope ||
        wvm_vcpu_handoff_request_decode(
            envelope->payload, envelope->payload_bytes, &request, error,
            error_len) != 0 ||
        wvm_vcpu_handoff_request_validate_envelope(
            &request, envelope, error, error_len) != 0) {
        if (!error || error[0] == '\0') {
            set_error(error, error_len,
                      "typed vCPU handoff input is invalid");
        }
        return -EINVAL;
    }
    if (wvm_x86_context_validate(
            request.backend, request.context, request.context_bytes,
            &encoded_context_fields, error, error_len) != 0 ||
        encoded_context_fields != request.context_valid_fields) {
        if (!error || error[0] == '\0') {
            set_error(error, error_len,
                      "typed vCPU handoff context does not match its schema");
        }
        return -EINVAL;
    }
    if (authorize_local_handoff(state, &request, envelope, error,
                                error_len) != 0) {
        return -EPERM;
    }

    memset(&operation, 0, sizeof(operation));
    operation.identity.vm_id = envelope->vm_id;
    operation.identity.vm_incarnation = envelope->vm_incarnation;
    operation.identity.manifest_generation = envelope->manifest_generation;
    operation.identity.local_runtime_instance_id =
        state->config.local_runtime_instance_id;
    memcpy(operation.identity.operation_id, envelope->operation_id,
           sizeof(operation.identity.operation_id));
    memcpy(operation.identity.candidate_manifest_digest,
           state->config.manifest->candidate_manifest_digest,
           sizeof(operation.identity.candidate_manifest_digest));
    operation.identity.route_snapshot_key.scope_key.vm_id = envelope->vm_id;
    operation.identity.route_snapshot_key.scope_key.vm_incarnation =
        envelope->vm_incarnation;
    operation.identity.route_snapshot_key.scope_key.route_scope_id =
        envelope->route_scope_id;
    operation.identity.route_snapshot_key.topology_revision =
        envelope->topology_revision;
    operation.identity.route_snapshot_key.route_generation =
        envelope->route_generation;
    memcpy(operation.identity.route_snapshot_key.snapshot_digest,
           envelope->route_snapshot_digest,
           sizeof(operation.identity.route_snapshot_key.snapshot_digest));
    memcpy(operation.identity.activation_fence,
           state->config.manifest->activation_fence,
           sizeof(operation.identity.activation_fence));
    operation.message_type = WVM_EXECUTOR_ABI_CPU_RUN;
    if (authorize_cpu_run(state, &operation, error, error_len) != 0) {
        return -EPERM;
    }

    replay = malloc(WVM_ENVELOPE_MAX_NETWORK_LOGICAL_PAYLOAD);
    if (!replay) {
        set_error(error, error_len, "cannot allocate typed vCPU replay buffer");
        return -ENOMEM;
    }
    cache_result = wvm_vcpu_handoff_cache_begin(
        &state->handoff_cache, &request, envelope, monotonic_milliseconds(),
        &cache_decision, replay, WVM_ENVELOPE_MAX_NETWORK_LOGICAL_PAYLOAD,
        &replay_bytes, error, error_len);
    if (cache_result == 0) {
        switch (cache_decision) {
        case WVM_VCPU_HANDOFF_CACHE_EXECUTE:
            /*
             * user_backend owns the RX buffer and releases it immediately
             * after ingress returns. The worker therefore receives an
             * immutable payload copy and never blocks the RX thread on local
             * execution or a legacy executor response.
             */
            cache_result = enqueue_typed_task(state, envelope);
            if (cache_result != 0) {
                handoff_result_from_request(
                    &request, WVM_VCPU_HANDOFF_RESULT_EXECUTOR_FAILURE,
                    &result);
                cache_result = cache_terminal_and_send(
                    state, envelope, &request, &result, error, error_len);
            }
            free(replay);
            return cache_result;
        case WVM_VCPU_HANDOFF_CACHE_IN_PROGRESS:
            handoff_result_from_request(
                &request, WVM_VCPU_HANDOFF_RESULT_IN_PROGRESS, &result);
            cache_result = send_typed_result(state, envelope, &request, &result,
                                             error, error_len);
            free(replay);
            return cache_result;
        case WVM_VCPU_HANDOFF_CACHE_REPLAY:
            if (wvm_vcpu_handoff_result_decode(replay, replay_bytes, &result,
                                               error, error_len) != 0 ||
                wvm_vcpu_handoff_result_validate_request(
                    &request, &result, error, error_len) != 0) {
                free(replay);
                return -EPROTO;
            }
            cache_result = send_typed_result(state, envelope, &request, &result,
                                             error, error_len);
            free(replay);
            return cache_result;
        case WVM_VCPU_HANDOFF_CACHE_RESULT_EXPIRED:
            handoff_result_from_request(
                &request, WVM_VCPU_HANDOFF_RESULT_EXPIRED, &result);
            cache_result = send_typed_result(state, envelope, &request, &result,
                                             error, error_len);
            free(replay);
            return cache_result;
        default:
            free(replay);
            set_error(error, error_len,
                      "vCPU handoff cache returned an unknown decision");
            return -EPROTO;
        }
    }

    if (cache_result == -EALREADY) {
        handoff_result_from_request(&request,
                                    WVM_VCPU_HANDOFF_RESULT_IN_PROGRESS,
                                    &result);
        cache_result = send_typed_result(state, envelope, &request, &result,
                                         error, error_len);
        free(replay);
        return cache_result;
    }
    if (cache_result == -ENOSPC || cache_result == -EAGAIN ||
        cache_result == -ENOBUFS) {
        handoff_result_from_request(&request,
                                    WVM_VCPU_HANDOFF_RESULT_BACKPRESSURE,
                                    &result);
        cache_result = send_typed_result(state, envelope, &request, &result,
                                         error, error_len);
        free(replay);
        return cache_result;
    }
    free(replay);
    return cache_result;
}

int wvm_executor_bridge_dispatch_init(
    const struct wvm_executor_bridge_config *config, void **dispatch_opaque,
    char *error, size_t error_len)
{
    struct bridge_state *state;
    int initialization_result;
    int mutex_initialized = 0;
    int condition_initialized = 0;
    int cache_initialized = 0;
    int worker_result;

    if (!dispatch_opaque) {
        set_error(error, error_len, "executor bridge dispatch output is missing");
        return -EINVAL;
    }
    *dispatch_opaque = NULL;
    if (config_is_admitted(config, error, error_len) != 0) {
        return -EINVAL;
    }
    state = calloc(1, sizeof(*state));
    if (!state) {
        set_error(error, error_len, "cannot allocate executor bridge state");
        return -ENOMEM;
    }
    state->config = *config;
    state->server_fd = -1;
    state->task_capacity =
        config->manifest->launch_plan.vcpu_handoff_record_capacity;
    state->next_legacy_request_id = monotonic_milliseconds();
    state->next_completed_memory_fence_id = monotonic_milliseconds();
    if (state->next_legacy_request_id == 0) {
        state->next_legacy_request_id = 1;
    }
    if (state->next_completed_memory_fence_id == 0) {
        state->next_completed_memory_fence_id = 1;
    }
    if (config->socket_path) {
        snprintf(state->socket_path, sizeof(state->socket_path), "%s",
                 config->socket_path);
    }
    state->tasks = calloc(state->task_capacity, sizeof(*state->tasks));
    if (!state->tasks) {
        initialization_result = -ENOMEM;
        set_error(error, error_len, "cannot allocate typed executor queue");
        goto fail;
    }
    initialization_result = pthread_mutex_init(&state->task_lock, NULL);
    if (initialization_result != 0) {
        initialization_result = -initialization_result;
        set_error(error, error_len, "cannot initialize typed executor lock");
        goto fail;
    }
    mutex_initialized = 1;
    initialization_result = pthread_cond_init(&state->task_available, NULL);
    if (initialization_result != 0) {
        initialization_result = -initialization_result;
        set_error(error, error_len,
                  "cannot initialize typed executor work condition");
        goto fail;
    }
    condition_initialized = 1;
    initialization_result = wvm_vcpu_handoff_cache_init(
        &state->handoff_cache, state->task_capacity,
        config->operation_retention_horizon_ms,
        WVM_ENVELOPE_MAX_NETWORK_LOGICAL_PAYLOAD, error, error_len);
    if (initialization_result != 0) {
        goto fail;
    }
    cache_initialized = 1;
    worker_result = pthread_create(&state->task_worker, NULL,
                                   typed_task_worker, state);
    if (worker_result != 0) {
        set_error(error, error_len, "cannot start typed executor worker");
        initialization_result = -worker_result;
        goto fail;
    }
    state->task_worker_started = 1;
    *dispatch_opaque = state;
    return 0;

fail:
    if (cache_initialized) {
        wvm_vcpu_handoff_cache_destroy(&state->handoff_cache);
    }
    if (condition_initialized) {
        pthread_cond_destroy(&state->task_available);
    }
    if (mutex_initialized) {
        pthread_mutex_destroy(&state->task_lock);
    }
    free(state->tasks);
    free(state);
    return initialization_result;
}

void wvm_executor_bridge_dispatch_destroy(void *dispatch_opaque)
{
    struct bridge_state *state = dispatch_opaque;
    int server_fd;
    size_t i;

    if (!state) {
        return;
    }
    pthread_mutex_lock(&state->task_lock);
    state->stopping = 1;
    server_fd = state->server_fd;
    state->server_fd = -1;
    pthread_cond_broadcast(&state->task_available);
    pthread_mutex_unlock(&state->task_lock);
    if (server_fd >= 0) {
        close(server_fd);
    }
    if (state->server_thread_started) {
        pthread_join(state->server_thread, NULL);
    }
    if (state->task_worker_started) {
        pthread_join(state->task_worker, NULL);
    }
    for (i = 0; i < state->task_capacity; i++) {
        task_release(&state->tasks[i]);
    }
    wvm_vcpu_handoff_cache_destroy(&state->handoff_cache);
    pthread_cond_destroy(&state->task_available);
    pthread_mutex_destroy(&state->task_lock);
    free(state->tasks);
    free(state);
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

static int open_executor_abi_listener(struct bridge_state *state)
{
    struct sockaddr_un address;
    int server_fd;

    if (!state || state->socket_path[0] == '\0') {
        return -EINVAL;
    }
    unlink(state->socket_path);
    server_fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (server_fd < 0) {
        return -errno;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s",
             state->socket_path);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        chmod(state->socket_path, 0600) != 0 ||
        listen(server_fd, 16) != 0) {
        int result = -errno;

        close(server_fd);
        unlink(state->socket_path);
        return result;
    }
    pthread_mutex_lock(&state->task_lock);
    if (state->stopping) {
        pthread_mutex_unlock(&state->task_lock);
        close(server_fd);
        unlink(state->socket_path);
        return -ESHUTDOWN;
    }
    state->server_fd = server_fd;
    pthread_mutex_unlock(&state->task_lock);
    return 0;
}

static void *executor_bridge_thread(void *opaque)
{
    struct bridge_state *state = opaque;
    int server_fd;

    pthread_mutex_lock(&state->task_lock);
    server_fd = state->server_fd;
    pthread_mutex_unlock(&state->task_lock);
    if (server_fd < 0) {
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
    pthread_mutex_lock(&state->task_lock);
    if (state->server_fd == server_fd) {
        state->server_fd = -1;
        close(server_fd);
    }
    pthread_mutex_unlock(&state->task_lock);
    unlink(state->socket_path);
    return NULL;
}

int wvm_executor_bridge_start(
    const struct wvm_executor_bridge_config *config, void **dispatch_opaque,
    pthread_t *thread_out)
{
    struct bridge_state *state;
    void *state_opaque = NULL;
    pthread_t thread;
    char error[256] = {0};
    int start_result;

    if (dispatch_opaque) {
        *dispatch_opaque = NULL;
    }
    if (!config || !config->socket_path ||
        config->socket_path[0] == '\0' ||
        strlen(config->socket_path) >= sizeof(state->socket_path) ||
        config->executor_service_port == 0 ||
        config->node_runtime_port == 0 ||
        config->local_runtime_instance_id == 0) {
        return -EINVAL;
    }
    if (wvm_executor_bridge_dispatch_init(
            config, &state_opaque, error, sizeof(error)) != 0) {
        return -EINVAL;
    }
    state = state_opaque;
    start_result = open_executor_abi_listener(state);
    if (start_result != 0) {
        wvm_executor_bridge_dispatch_destroy(state);
        return start_result;
    }
    start_result = pthread_create(&thread, NULL, executor_bridge_thread, state);
    if (start_result != 0) {
        wvm_executor_bridge_dispatch_destroy(state);
        return -start_result;
    }
    state->server_thread = thread;
    state->server_thread_started = 1;
    if (dispatch_opaque) {
        *dispatch_opaque = state;
    }
    if (thread_out) {
        *thread_out = thread;
    }
    return 0;
}
