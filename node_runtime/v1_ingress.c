#define _POSIX_C_SOURCE 200809L

#include "v1_ingress.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../common_include/wavevm_memory_v1.h"

struct ingress_thread_state {
    int initialized;
    struct wvm_envelope_v1_reassembler reassembler;
};

static struct wvm_v1_ingress g_ingress;
static _Thread_local struct ingress_thread_state g_thread_ingress;

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
    return (uint64_t)now.tv_sec * 1000U +
           (uint64_t)now.tv_nsec / 1000000U;
}

static int route_key_from_envelope(
    const struct wvm_envelope_v1 *envelope,
    struct wvm_route_snapshot_key *route_key)
{
    if (!envelope || !route_key) {
        return -1;
    }
    memset(route_key, 0, sizeof(*route_key));
    route_key->scope_key.vm_id = envelope->vm_id;
    route_key->scope_key.vm_incarnation = envelope->vm_incarnation;
    route_key->scope_key.route_scope_id = envelope->route_scope_id;
    route_key->topology_revision = envelope->topology_revision;
    route_key->route_generation = envelope->route_generation;
    memcpy(route_key->snapshot_digest, envelope->route_snapshot_digest,
           sizeof(route_key->snapshot_digest));
    return 0;
}

static int authorize_envelope(const struct wvm_v1_ingress *ingress,
                              const struct wvm_envelope_v1 *envelope,
                              char *error, size_t error_len)
{
    struct wvm_runtime_operation operation;

    if (!ingress || !envelope) {
        set_error(error, error_len, "V1 ingress authorization input is invalid");
        return -1;
    }
    memset(&operation, 0, sizeof(operation));
    operation.connection_id = ingress->config.runtime_connection_id;
    operation.vm_id = envelope->vm_id;
    operation.vm_incarnation = envelope->vm_incarnation;
    operation.manifest_generation = envelope->manifest_generation;
    memcpy(operation.candidate_manifest_digest,
           ingress->config.manifest->candidate_manifest_digest,
           sizeof(operation.candidate_manifest_digest));
    route_key_from_envelope(envelope, &operation.route_snapshot_key);
    memcpy(operation.activation_fence,
           ingress->config.manifest->activation_fence,
           sizeof(operation.activation_fence));
    memcpy(operation.operation_id, envelope->operation_id,
           sizeof(operation.operation_id));
    return wvm_runtime_gate_authorize(ingress->config.runtime_gate, &operation,
                                      error, error_len);
}

static int dispatch_complete_envelope(struct wvm_v1_ingress *ingress,
                                      const struct wvm_envelope_v1 *envelope,
                                      char *error, size_t error_len)
{
    int dispatch_result;

    if (wvm_envelope_v1_validate_admitted(envelope,
                                          &ingress->admitted_identity, error,
                                          error_len) != 0 ||
        authorize_envelope(ingress, envelope, error, error_len) != 0 ||
        wvm_v1_memory_payload_validate(envelope->message_type,
                                       envelope->payload,
                                       envelope->payload_bytes, error,
                                       error_len) != 0) {
        return WVM_V1_INGRESS_REJECTED;
    }

    dispatch_result = ingress->config.dispatch(
        ingress->config.dispatch_opaque, envelope, error, error_len);
    if (dispatch_result == 0) {
        return WVM_V1_INGRESS_ACCEPTED;
    }
    if (dispatch_result == -EAGAIN || dispatch_result == -ENOBUFS) {
        if (!error || error[0] == '\0') {
            set_error(error, error_len, "local V1 executor queue is full");
        }
        return WVM_V1_INGRESS_BACKPRESSURE;
    }
    if (dispatch_result == -ENOTSUP) {
        if (!error || error[0] == '\0') {
            set_error(error, error_len,
                      "V1 operation payload schema is not implemented");
        }
        return WVM_V1_INGRESS_UNSUPPORTED;
    }
    if (!error || error[0] == '\0') {
        set_error(error, error_len, "local V1 dispatcher rejected operation");
    }
    return WVM_V1_INGRESS_REJECTED;
}

int wvm_v1_ingress_init(struct wvm_v1_ingress *ingress,
                        const struct wvm_v1_ingress_config *config,
                        char *error, size_t error_len)
{
    const struct wvm_node_runtime_manifest *manifest;

    if (!ingress || !config || !config->manifest || !config->runtime_gate ||
        config->runtime_connection_id == 0 || !config->dispatch) {
        set_error(error, error_len, "V1 ingress configuration is incomplete");
        return -1;
    }
    manifest = config->manifest;
    if (config->runtime_gate->state != WVM_RUNTIME_GATE_ACTIVE ||
        config->runtime_gate->manifest != manifest ||
        !manifest->has_activation_fence ||
        !(manifest->local_role_bits &
          WVM_RUNTIME_ROLE_BIT(WVM_MANIFEST_ROLE_NODE_RUNTIME)) ||
        manifest->required_route_snapshot_key.scope_key.vm_id !=
            manifest->vm_id ||
        manifest->required_route_snapshot_key.scope_key.vm_incarnation !=
            manifest->vm_incarnation ||
        wvm_route_snapshot_key_validate(
            &manifest->required_route_snapshot_key, error, error_len) != 0) {
        if (!error || error[0] == '\0') {
            set_error(error, error_len,
                      "V1 ingress manifest has no admitted node-runtime route");
        }
        return -1;
    }

    memset(ingress, 0, sizeof(*ingress));
    ingress->config = *config;
    ingress->admitted_identity.vm_id = manifest->vm_id;
    ingress->admitted_identity.vm_incarnation = manifest->vm_incarnation;
    ingress->admitted_identity.manifest_generation =
        manifest->manifest_generation;
    ingress->admitted_identity.route_scope_id =
        manifest->required_route_snapshot_key.scope_key.route_scope_id;
    ingress->admitted_identity.topology_revision =
        manifest->required_route_snapshot_key.topology_revision;
    ingress->admitted_identity.route_generation =
        manifest->required_route_snapshot_key.route_generation;
    memcpy(ingress->admitted_identity.route_snapshot_digest,
           manifest->required_route_snapshot_key.snapshot_digest,
           sizeof(ingress->admitted_identity.route_snapshot_digest));
    ingress->initialized = 1;
    return 0;
}

int wvm_v1_ingress_accept(struct wvm_v1_ingress *ingress,
                          struct wvm_envelope_v1_reassembler *reassembler,
                          const uint8_t *frame, size_t frame_bytes,
                          uint64_t now_ms, char *error, size_t error_len)
{
    struct wvm_envelope_v1 envelope;

    if (!ingress || !ingress->initialized || !reassembler || !frame ||
        frame_bytes == 0) {
        set_error(error, error_len, "V1 ingress input is invalid");
        return WVM_V1_INGRESS_REJECTED;
    }
    if (wvm_envelope_v1_decode(frame, frame_bytes,
                               WVM_ENVELOPE_V1_TRANSPORT_NETWORK, &envelope,
                               error, error_len) != 0) {
        return WVM_V1_INGRESS_REJECTED;
    }
    /*
     * Check frame identity before retaining a fragmented payload. This keeps
     * stale VM/route traffic from consuming the bounded reassembly budget.
     */
    if (wvm_envelope_v1_validate_admitted(&envelope,
                                          &ingress->admitted_identity, error,
                                          error_len) != 0) {
        return WVM_V1_INGRESS_REJECTED;
    }
    if (!(envelope.flags & WVM_ENVELOPE_V1_FLAG_FRAGMENTED)) {
        return dispatch_complete_envelope(ingress, &envelope, error, error_len);
    }

    {
        struct wvm_envelope_v1_reassembled reassembled;
        int reassembly_result;

        memset(&reassembled, 0, sizeof(reassembled));
        reassembly_result = wvm_envelope_v1_reassembler_accept(
            reassembler, &envelope, now_ms, &reassembled, error, error_len);
        if (reassembly_result < 0) {
            return WVM_V1_INGRESS_REJECTED;
        }
        if (reassembly_result == 0) {
            return WVM_V1_INGRESS_INCOMPLETE;
        }
        reassembly_result = dispatch_complete_envelope(
            ingress, &reassembled.envelope, error, error_len);
        wvm_envelope_v1_reassembled_release(&reassembled);
        return reassembly_result;
    }
}

int wvm_v1_ingress_global_init(const struct wvm_v1_ingress_config *config,
                               char *error, size_t error_len)
{
    /*
     * The runtime is initialized exactly once before master RX threads start.
     * Refusing a second initialization prevents an active manifest/gate from
     * being swapped under concurrent packet processing.
     */
    if (g_ingress.initialized) {
        set_error(error, error_len, "V1 ingress is already initialized");
        return -1;
    }
    return wvm_v1_ingress_init(&g_ingress, config, error, error_len);
}

int wvm_v1_ingress_handle_datagram(const uint8_t *frame, size_t frame_bytes)
{
    char error[128] = {0};

    if (!g_ingress.initialized) {
        return WVM_V1_INGRESS_REJECTED;
    }
    if (!g_thread_ingress.initialized) {
        if (wvm_envelope_v1_reassembler_init(&g_thread_ingress.reassembler) !=
            0) {
            return WVM_V1_INGRESS_BACKPRESSURE;
        }
        g_thread_ingress.initialized = 1;
    }
    return wvm_v1_ingress_accept(&g_ingress, &g_thread_ingress.reassembler,
                                 frame, frame_bytes, monotonic_milliseconds(),
                                 error, sizeof(error));
}
