#ifndef WAVEVM_LOCAL_MEMORY_V1_H
#define WAVEVM_LOCAL_MEMORY_V1_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_identity.h"
#include "wavevm_memory_v1.h"

/*
 * QEMU/executor channels are manifest-bound at registration time. This local
 * record therefore carries the semantic operation key and page request while
 * resolving VM identity and route authority from that admitted connection.
 */
#define WVM_LOCAL_MEMORY_V1_FAULT_REQUEST_BYTES 32U
#define WVM_LOCAL_MEMORY_V1_RESULT_LENGTH_BYTES 4U
#define WVM_LOCAL_MEMORY_V1_COMMIT_REQUEST_HEADER_BYTES \
    (WVM_IDENTITY_ID_BYTES + 8U + WVM_V1_MEM_COMMIT_HEADER_BYTES)
#define WVM_LOCAL_MEMORY_V1_COMMIT_RESULT_BYTES \
    (WVM_IDENTITY_ID_BYTES + WVM_V1_MEM_COMMIT_ACK_BYTES)

struct wvm_local_memory_v1_fault_request {
    uint8_t operation_id[WVM_IDENTITY_ID_BYTES];
    uint64_t delivery_attempt_id;
    uint64_t gpa;
};

struct wvm_local_memory_v1_commit_request {
    uint8_t operation_id[WVM_IDENTITY_ID_BYTES];
    uint64_t delivery_attempt_id;
    struct wvm_v1_mem_commit commit;
};

struct wvm_local_memory_v1_commit_result {
    uint8_t operation_id[WVM_IDENTITY_ID_BYTES];
    struct wvm_v1_mem_commit_ack ack;
};

int wvm_local_memory_v1_fault_request_encode(
    const struct wvm_local_memory_v1_fault_request *request,
    uint8_t output[WVM_LOCAL_MEMORY_V1_FAULT_REQUEST_BYTES], char *error,
    size_t error_len);
int wvm_local_memory_v1_fault_request_decode(
    const uint8_t *input, size_t input_bytes,
    struct wvm_local_memory_v1_fault_request *request, char *error,
    size_t error_len);

int wvm_local_memory_v1_commit_request_encode(
    const struct wvm_local_memory_v1_commit_request *request,
    uint8_t *output, size_t output_capacity, size_t *output_bytes,
    char *error, size_t error_len);
int wvm_local_memory_v1_commit_request_decode(
    const uint8_t *input, size_t input_bytes,
    struct wvm_local_memory_v1_commit_request *request, char *error,
    size_t error_len);
int wvm_local_memory_v1_commit_result_encode(
    const struct wvm_local_memory_v1_commit_result *result,
    uint8_t output[WVM_LOCAL_MEMORY_V1_COMMIT_RESULT_BYTES], char *error,
    size_t error_len);
int wvm_local_memory_v1_commit_result_decode(
    const uint8_t input[WVM_LOCAL_MEMORY_V1_COMMIT_RESULT_BYTES],
    struct wvm_local_memory_v1_commit_result *result, char *error,
    size_t error_len);

/*
 * A zero result length denotes a local terminal failure before a directory
 * ACK exists. A nonzero length is one exact typed V1 MEM_ACK payload.
 */
int wvm_local_memory_v1_result_length_encode(
    size_t ack_payload_bytes,
    uint8_t output[WVM_LOCAL_MEMORY_V1_RESULT_LENGTH_BYTES], char *error,
    size_t error_len);
int wvm_local_memory_v1_result_length_decode(
    const uint8_t input[WVM_LOCAL_MEMORY_V1_RESULT_LENGTH_BYTES],
    size_t *ack_payload_bytes, char *error, size_t error_len);

#endif /* WAVEVM_LOCAL_MEMORY_V1_H */
