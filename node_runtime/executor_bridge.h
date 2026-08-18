#ifndef WAVEVM_EXECUTOR_BRIDGE_H
#define WAVEVM_EXECUTOR_BRIDGE_H

#include <stddef.h>
#include <pthread.h>
#include <stdint.h>

#include "../common_include/wavevm_envelope.h"
#include "../common_include/wavevm_route_runtime.h"
#include "../common_include/wavevm_runtime_dispatch.h"
#include "../common_include/wavevm_runtime_gate.h"

/*
 * The bridge submits complete immutable envelopes to the local
 * sidecar/gateway boundary. The callback must consume the envelope before it
 * returns; bridge-owned result storage is released immediately afterward.
 */
typedef int (*wvm_executor_bridge_send_envelope_fn)(
    void *opaque, const struct wvm_envelope *envelope, char *error,
    size_t error_len);

struct wvm_executor_bridge_config {
    const struct wvm_node_runtime_manifest *manifest;
    const struct wvm_runtime_gate *runtime_gate;
    const struct wvm_runtime_dispatch_projection *dispatch;
    const struct wvm_route_runtime *route_runtime;
    uint64_t local_runtime_instance_id;
    uint64_t runtime_connection_id;
    uint64_t operation_retention_horizon_ms;
    const char *socket_path;
    uint16_t executor_service_port;
    uint16_t node_runtime_port;
    wvm_executor_bridge_send_envelope_fn send_envelope;
    void *send_envelope_opaque;
};

/*
 * Create/destroy the typed ingress dispatcher without opening the legacy
 * executor ABI socket. The node runtime uses this state through
 * wvm_executor_bridge_dispatch; tests and other local lifecycle owners can
 * exercise the same admitted data-path boundary directly.
 */
int wvm_executor_bridge_dispatch_init(
    const struct wvm_executor_bridge_config *config, void **dispatch_opaque,
    char *error, size_t error_len);
void wvm_executor_bridge_dispatch_destroy(void *dispatch_opaque);

int wvm_executor_bridge_start(
    const struct wvm_executor_bridge_config *config, void **dispatch_opaque,
    pthread_t *thread_out);

/*
 * Typed vCPU ingress reaches the executor only through this adapter. It
 * independently rechecks the manifest-bound executor identity and local
 * RouteKey before producing a typed VCPU_EXIT through the local sidecar.
 */
int wvm_executor_bridge_dispatch(
    void *opaque, const struct wvm_envelope *envelope, char *error,
    size_t error_len);

#endif
