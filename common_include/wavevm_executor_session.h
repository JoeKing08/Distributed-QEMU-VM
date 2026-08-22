#ifndef WAVEVM_EXECUTOR_SESSION_H
#define WAVEVM_EXECUTOR_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_envelope.h"

#define WVM_EXECUTOR_SESSION_MAGIC 0x57565331U /* "WVS1" */
#define WVM_EXECUTOR_SESSION_VERSION 1U
#define WVM_EXECUTOR_SESSION_HEADER_BYTES 16U
#define WVM_EXECUTOR_SESSION_MAX_FRAME_BYTES \
    (WVM_EXECUTOR_SESSION_HEADER_BYTES + WVM_ENVELOPE_MAX_NETWORK_LOGICAL_PAYLOAD)

enum wvm_executor_session_message_type {
    WVM_EXECUTOR_SESSION_VCPU_RUN = 1,
    WVM_EXECUTOR_SESSION_VCPU_EXIT = 2,
};

int wvm_executor_session_encode(
    uint16_t message_type, const uint8_t *payload, size_t payload_bytes,
    uint8_t *output, size_t output_capacity, size_t *output_bytes,
    char *error, size_t error_len);

int wvm_executor_session_decode(
    const uint8_t *input, size_t input_bytes, uint16_t *message_type,
    const uint8_t **payload, size_t *payload_bytes, char *error,
    size_t error_len);

#endif /* WAVEVM_EXECUTOR_SESSION_H */
