#include "wavevm_executor_session.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum {
    SESSION_MAGIC_OFFSET = 0,
    SESSION_VERSION_OFFSET = 4,
    SESSION_TYPE_OFFSET = 6,
    SESSION_PAYLOAD_BYTES_OFFSET = 8,
    SESSION_RESERVED_OFFSET = 12,
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

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static uint16_t read_be16(const uint8_t *bytes)
{
    return ((uint16_t)bytes[0] << 8) | bytes[1];
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static int message_type_valid(uint16_t message_type)
{
    return message_type == WVM_EXECUTOR_SESSION_VCPU_RUN ||
           message_type == WVM_EXECUTOR_SESSION_VCPU_EXIT;
}

int wvm_executor_session_encode(
    uint16_t message_type, const uint8_t *payload, size_t payload_bytes,
    uint8_t *output, size_t output_capacity, size_t *output_bytes,
    char *error, size_t error_len)
{
    if (!message_type_valid(message_type) || !output || !output_bytes ||
        (payload_bytes != 0 && !payload) ||
        payload_bytes > WVM_ENVELOPE_MAX_NETWORK_LOGICAL_PAYLOAD ||
        output_capacity < WVM_EXECUTOR_SESSION_HEADER_BYTES + payload_bytes) {
        set_error(error, error_len, "executor session frame is invalid");
        return -1;
    }

    memset(output, 0, WVM_EXECUTOR_SESSION_HEADER_BYTES);
    write_be32(output + SESSION_MAGIC_OFFSET, WVM_EXECUTOR_SESSION_MAGIC);
    write_be16(output + SESSION_VERSION_OFFSET,
               WVM_EXECUTOR_SESSION_VERSION);
    write_be16(output + SESSION_TYPE_OFFSET, message_type);
    write_be32(output + SESSION_PAYLOAD_BYTES_OFFSET,
               (uint32_t)payload_bytes);
    if (payload_bytes != 0) {
        memcpy(output + WVM_EXECUTOR_SESSION_HEADER_BYTES, payload,
               payload_bytes);
    }
    *output_bytes = WVM_EXECUTOR_SESSION_HEADER_BYTES + payload_bytes;
    return 0;
}

int wvm_executor_session_decode(
    const uint8_t *input, size_t input_bytes, uint16_t *message_type,
    const uint8_t **payload, size_t *payload_bytes, char *error,
    size_t error_len)
{
    uint32_t encoded_payload_bytes;

    if (!input || input_bytes < WVM_EXECUTOR_SESSION_HEADER_BYTES ||
        !message_type || !payload || !payload_bytes ||
        read_be32(input + SESSION_MAGIC_OFFSET) !=
            WVM_EXECUTOR_SESSION_MAGIC ||
        read_be16(input + SESSION_VERSION_OFFSET) !=
            WVM_EXECUTOR_SESSION_VERSION ||
        read_be32(input + SESSION_RESERVED_OFFSET) != 0) {
        set_error(error, error_len, "executor session header is invalid");
        return -1;
    }

    encoded_payload_bytes = read_be32(input + SESSION_PAYLOAD_BYTES_OFFSET);
    if (!message_type_valid(read_be16(input + SESSION_TYPE_OFFSET)) ||
        encoded_payload_bytes > WVM_ENVELOPE_MAX_NETWORK_LOGICAL_PAYLOAD ||
        input_bytes != WVM_EXECUTOR_SESSION_HEADER_BYTES +
                            (size_t)encoded_payload_bytes) {
        set_error(error, error_len, "executor session frame bounds are invalid");
        return -1;
    }
    *message_type = read_be16(input + SESSION_TYPE_OFFSET);
    *payload = input + WVM_EXECUTOR_SESSION_HEADER_BYTES;
    *payload_bytes = encoded_payload_bytes;
    return 0;
}
