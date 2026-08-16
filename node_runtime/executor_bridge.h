#ifndef WAVEVM_EXECUTOR_BRIDGE_H
#define WAVEVM_EXECUTOR_BRIDGE_H

#include <pthread.h>
#include <stdint.h>

#include "../common_include/wavevm_envelope_v1.h"
#include "../common_include/wavevm_runtime_gate.h"

struct wvm_executor_bridge_config {
    const struct wvm_node_runtime_manifest *manifest;
    const struct wvm_runtime_gate *runtime_gate;
    uint64_t local_runtime_instance_id;
    uint64_t runtime_connection_id;
    const char *socket_path;
    uint16_t executor_service_port;
    uint16_t node_runtime_port;
};

int wvm_executor_bridge_start(
    const struct wvm_executor_bridge_config *config, pthread_t *thread_out);

/*
 * V1 ingress enters local execution only through this adapter. The current
 * executor ABI still has a legacy wvm_header payload and therefore cannot
 * safely reinterpret a V1 semantic payload. It returns -ENOTSUP until a
 * typed executor record is implemented.
 */
int wvm_executor_bridge_v1_compat_dispatch(
    void *opaque, const struct wvm_envelope_v1 *envelope, char *error,
    size_t error_len);

#endif
