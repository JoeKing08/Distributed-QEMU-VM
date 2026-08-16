#ifndef WAVEVM_NODE_RUNTIME_V1_INGRESS_H
#define WAVEVM_NODE_RUNTIME_V1_INGRESS_H

#include <stddef.h>
#include <stdint.h>

#include "../common_include/wavevm_envelope_v1.h"
#include "../common_include/wavevm_runtime_gate.h"

/*
 * A V1 frame reaches this boundary only after gateway/sidecar forwarding.
 * The dispatcher is local-only: it may submit to a local executor adapter,
 * but it must never create a direct remote-executor path.
 *
 * Return 0 after taking responsibility for the immutable envelope, -EAGAIN
 * for bounded local backpressure, -ENOTSUP for an operation whose semantic
 * payload schema has not been implemented, and another negative errno value
 * for a terminal local rejection.
 */
typedef int (*wvm_v1_ingress_dispatch_fn)(
    void *opaque, const struct wvm_envelope_v1 *envelope, char *error,
    size_t error_len);

enum wvm_v1_ingress_status {
    WVM_V1_INGRESS_ACCEPTED = 0,
    WVM_V1_INGRESS_INCOMPLETE = 1,
    WVM_V1_INGRESS_REJECTED = 2,
    WVM_V1_INGRESS_BACKPRESSURE = 3,
    WVM_V1_INGRESS_UNSUPPORTED = 4,
};

struct wvm_v1_ingress_config {
    const struct wvm_node_runtime_manifest *manifest;
    const struct wvm_runtime_gate *runtime_gate;
    uint64_t runtime_connection_id;
    wvm_v1_ingress_dispatch_fn dispatch;
    void *dispatch_opaque;
};

struct wvm_v1_ingress {
    struct wvm_v1_ingress_config config;
    struct wvm_envelope_v1_admitted_identity admitted_identity;
    int initialized;
};

/*
 * The caller owns REASSEMBLER. RX threads keep one instance each, which
 * bounds fragment state without putting unrelated V1 packets behind a global
 * ingress lock.
 */
int wvm_v1_ingress_init(struct wvm_v1_ingress *ingress,
                        const struct wvm_v1_ingress_config *config,
                        char *error, size_t error_len);

int wvm_v1_ingress_accept(struct wvm_v1_ingress *ingress,
                          struct wvm_envelope_v1_reassembler *reassembler,
                          const uint8_t *frame, size_t frame_bytes,
                          uint64_t now_ms, char *error, size_t error_len);

/*
 * Unified node-runtime entry. It is initialized before the legacy master RX
 * threads start. Standalone legacy masters intentionally leave this symbol
 * unresolved weakly and continue to ignore V1 datagrams.
 */
int wvm_v1_ingress_global_init(const struct wvm_v1_ingress_config *config,
                               char *error, size_t error_len);
int wvm_v1_ingress_handle_datagram(const uint8_t *frame, size_t frame_bytes);

#endif /* WAVEVM_NODE_RUNTIME_V1_INGRESS_H */
