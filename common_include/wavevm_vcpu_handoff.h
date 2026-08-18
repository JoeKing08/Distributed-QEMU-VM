#ifndef WAVEVM_VCPU_HANDOFF_H
#define WAVEVM_VCPU_HANDOFF_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_envelope.h"

#define WVM_VCPU_HANDOFF_REQUEST_VERSION 1U
#define WVM_VCPU_HANDOFF_REQUEST_HEADER_BYTES 168U
#define WVM_VCPU_HANDOFF_RESULT_VERSION 1U
#define WVM_VCPU_HANDOFF_RESULT_HEADER_BYTES 160U
#define WVM_VCPU_HANDOFF_MAX_CONTEXT_BYTES \
    (WVM_ENVELOPE_MAX_NETWORK_LOGICAL_PAYLOAD - \
     WVM_VCPU_HANDOFF_REQUEST_HEADER_BYTES)
#define WVM_VCPU_HANDOFF_MAX_RESULT_CONTEXT_BYTES \
    (WVM_ENVELOPE_MAX_NETWORK_LOGICAL_PAYLOAD - \
     WVM_VCPU_HANDOFF_RESULT_HEADER_BYTES)
#define WVM_VCPU_CONTEXT_SCHEMA_X86 1U

enum wvm_vcpu_backend {
    WVM_VCPU_BACKEND_KVM = 1,
    WVM_VCPU_BACKEND_TCG = 2,
};

enum wvm_vcpu_memory_fence_result {
    WVM_VCPU_MEMORY_FENCE_SUCCEEDED = 1,
};

enum wvm_vcpu_handoff_result_status {
    WVM_VCPU_HANDOFF_RESULT_SUCCESS = 0,
    WVM_VCPU_HANDOFF_RESULT_EXECUTOR_FAILURE = 1,
    WVM_VCPU_HANDOFF_RESULT_MEMORY_FAILURE = 2,
    WVM_VCPU_HANDOFF_RESULT_STALE = 3,
    WVM_VCPU_HANDOFF_RESULT_BACKPRESSURE = 4,
    WVM_VCPU_HANDOFF_RESULT_EXPIRED = 5,
    WVM_VCPU_HANDOFF_RESULT_IN_PROGRESS = 6,
};

enum wvm_vcpu_exit_class {
    WVM_VCPU_EXIT_NONE = 0,
    WVM_VCPU_EXIT_BUDGET = 1,
    WVM_VCPU_EXIT_HALTED = 2,
    WVM_VCPU_EXIT_PIO = 3,
    WVM_VCPU_EXIT_MMIO = 4,
    WVM_VCPU_EXIT_INTERRUPT = 5,
    WVM_VCPU_EXIT_EXCEPTION = 6,
    WVM_VCPU_EXIT_MEMORY_ERROR = 7,
    WVM_VCPU_EXIT_EXECUTOR_ERROR = 8,
};

enum wvm_vcpu_context_field {
    WVM_VCPU_CONTEXT_FIELD_ARCHITECTURAL_STATE = 1ULL << 0,
    WVM_VCPU_CONTEXT_FIELD_INTERRUPT_STATE = 1ULL << 1,
    WVM_VCPU_CONTEXT_FIELD_TIMER_STATE = 1ULL << 2,
    WVM_VCPU_CONTEXT_FIELD_DEVICE_RESUME = 1ULL << 3,
};

#define WVM_VCPU_CONTEXT_KNOWN_FIELDS \
    (WVM_VCPU_CONTEXT_FIELD_ARCHITECTURAL_STATE | \
     WVM_VCPU_CONTEXT_FIELD_INTERRUPT_STATE | \
     WVM_VCPU_CONTEXT_FIELD_TIMER_STATE | \
     WVM_VCPU_CONTEXT_FIELD_DEVICE_RESUME)

/*
 * The envelope supplies VM/route forwarding metadata and its semantic digest.
 * This record adds the vCPU-specific operation identity and an independently
 * checked digest for the opaque, schema-versioned CPU context.
 */
struct wvm_vcpu_handoff_request {
    uint16_t protocol_version;
    uint16_t backend;
    uint16_t context_schema_version;
    uint16_t memory_fence_result;
    uint32_t vm_id;
    uint32_t origin_physical_node_id;
    uint32_t vcpu_index;
    uint64_t vm_incarnation;
    uint64_t manifest_generation;
    uint64_t origin_runtime_instance_id;
    uint64_t destination_executor_id;
    uint16_t reply_destination_kind;
    uint64_t reply_destination_scope;
    uint32_t reply_destination_vnode;
    uint64_t handoff_sequence;
    uint64_t memory_fence_id;
    uint64_t local_interrupt_watermark;
    uint64_t device_event_watermark;
    uint8_t operation_id[16];
    uint64_t context_valid_fields;
    const uint8_t *context;
    size_t context_bytes;
};

struct wvm_vcpu_handoff_result {
    uint16_t protocol_version;
    uint16_t status;
    uint16_t exit_class;
    uint16_t backend;
    uint32_t vm_id;
    uint32_t origin_physical_node_id;
    uint32_t vcpu_index;
    uint64_t vm_incarnation;
    uint64_t manifest_generation;
    uint64_t origin_runtime_instance_id;
    uint64_t handoff_sequence;
    uint64_t error_gpa;
    uint64_t produced_memory_fence_id;
    uint64_t remote_interrupt_watermark;
    uint64_t remote_device_watermark;
    uint8_t operation_id[16];
    uint16_t context_schema_version;
    uint64_t context_valid_fields;
    const uint8_t *context;
    size_t context_bytes;
};

int wvm_vcpu_handoff_request_encode(
    const struct wvm_vcpu_handoff_request *request, uint8_t *output,
    size_t output_capacity, size_t *output_bytes, char *error,
    size_t error_len);

int wvm_vcpu_handoff_request_decode(
    const uint8_t *input, size_t input_bytes,
    struct wvm_vcpu_handoff_request *request, char *error, size_t error_len);

/*
 * Verify that the typed request and its outer envelope describe the same
 * immutable semantic operation. Mutable forwarding fields remain owned by the
 * envelope route prefix and are intentionally not duplicated here.
 */
int wvm_vcpu_handoff_request_validate_envelope(
    const struct wvm_vcpu_handoff_request *request,
    const struct wvm_envelope *envelope, char *error, size_t error_len);

int wvm_vcpu_handoff_result_encode(
    const struct wvm_vcpu_handoff_result *result, uint8_t *output,
    size_t output_capacity, size_t *output_bytes, char *error,
    size_t error_len);

int wvm_vcpu_handoff_result_decode(
    const uint8_t *input, size_t input_bytes,
    struct wvm_vcpu_handoff_result *result, char *error, size_t error_len);

int wvm_vcpu_handoff_result_validate_request(
    const struct wvm_vcpu_handoff_request *request,
    const struct wvm_vcpu_handoff_result *result, char *error,
    size_t error_len);

/*
 * Build one typed VCPU_EXIT for an admitted request. The request carries the
 * complete reply leaf RouteKey; callers resolve that key against the active
 * immutable route snapshot before calling this function.
 */
int wvm_vcpu_handoff_exit_envelope_build(
    const struct wvm_envelope *request_envelope,
    const struct wvm_vcpu_handoff_request *request,
    const struct wvm_envelope_route *resolved_reply_route,
    uint64_t response_delivery_attempt_id,
    const struct wvm_vcpu_handoff_result *result, uint8_t *payload_output,
    size_t payload_output_capacity, size_t *payload_output_bytes,
    struct wvm_envelope *response, char *error, size_t error_len);

/*
 * Validate a returned typed exit against the original handoff and its reply
 * RouteKey. Hop progress remains forwarding metadata and is not constrained
 * here beyond the response targeting the admitted reply leaf.
 */
int wvm_vcpu_handoff_exit_validate_envelope(
    const struct wvm_vcpu_handoff_request *request,
    const struct wvm_vcpu_handoff_result *result,
    const struct wvm_envelope *envelope, char *error, size_t error_len);

#endif /* WAVEVM_VCPU_HANDOFF_H */
