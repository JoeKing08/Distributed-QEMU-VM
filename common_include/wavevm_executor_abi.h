#ifndef WAVEVM_EXECUTOR_ABI_H
#define WAVEVM_EXECUTOR_ABI_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_lifecycle.h"

#define WVM_EXECUTOR_ABI_MAGIC 0x57564531U /* "WVE1" */
#define WVM_EXECUTOR_ABI_VERSION 2U
#define WVM_EXECUTOR_ABI_CPU_RUN 1U
#define WVM_EXECUTOR_ABI_RESULT 2U
#define WVM_EXECUTOR_ABI_HEADER_BYTES 196U
#define WVM_EXECUTOR_ABI_MAX_PAYLOAD (4U * 1024U * 1024U)

enum wvm_executor_abi_status {
    WVM_EXECUTOR_ABI_SUCCESS = 0,
    WVM_EXECUTOR_ABI_INVALID_FRAME = 1,
    WVM_EXECUTOR_ABI_STALE_IDENTITY = 2,
    WVM_EXECUTOR_ABI_BACKPRESSURE = 3,
    WVM_EXECUTOR_ABI_UNSUPPORTED = 4,
    WVM_EXECUTOR_ABI_INTERNAL_FAILURE = 5,
};

struct wvm_executor_abi_identity {
    uint32_t vm_id;
    uint64_t vm_incarnation;
    uint64_t manifest_generation;
    uint64_t local_runtime_instance_id;
    uint8_t operation_id[16];
    uint8_t candidate_manifest_digest[WVM_SHA256_DIGEST_BYTES];
    struct wvm_route_snapshot_key route_snapshot_key;
    uint8_t activation_fence[WVM_IDENTITY_ID_BYTES];
};

struct wvm_executor_abi_frame {
    struct wvm_executor_abi_identity identity;
    uint16_t message_type;
    uint16_t status;
    const uint8_t *payload;
    size_t payload_bytes;
};

int wvm_executor_abi_encode(
    const struct wvm_executor_abi_frame *frame, uint8_t *output,
    size_t capacity, size_t *encoded_bytes, char *error, size_t error_len);

int wvm_executor_abi_decode(
    const uint8_t *input, size_t input_bytes,
    struct wvm_executor_abi_frame *frame, char *error, size_t error_len);

int wvm_executor_abi_validate_identity(
    const struct wvm_executor_abi_frame *frame,
    const struct wvm_node_runtime_manifest *manifest,
    uint64_t local_runtime_instance_id, char *error, size_t error_len);

int wvm_executor_abi_validate_result(
    const struct wvm_executor_abi_frame *request,
    const struct wvm_executor_abi_frame *result, char *error,
    size_t error_len);

#endif
