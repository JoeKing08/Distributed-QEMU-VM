#include "wavevm_x86_context.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "wavevm_protocol.h"

enum {
    CONTEXT_MAGIC_OFFSET = 0,
    CONTEXT_SCHEMA_OFFSET = 4,
    CONTEXT_BACKEND_OFFSET = 6,
    CONTEXT_FIELDS_OFFSET = 8,
    CONTEXT_BODY_BYTES_OFFSET = 16,
    CONTEXT_RESERVED_OFFSET = 20,
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

static void write_be64(uint8_t *bytes, uint64_t value)
{
    size_t i;

    for (i = 0; i < 8; i++) {
        bytes[7U - i] = (uint8_t)(value >> (i * 8U));
    }
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

static uint64_t read_be64(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t i;

    for (i = 0; i < 8; i++) {
        value = (value << 8) | bytes[i];
    }
    return value;
}

static int all_zero(const uint8_t *bytes, size_t byte_count)
{
    size_t i;

    for (i = 0; i < byte_count; i++) {
        if (bytes[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static int fields_valid(uint64_t valid_fields)
{
    return (valid_fields & WVM_VCPU_CONTEXT_FIELD_ARCHITECTURAL_STATE) != 0 &&
           (valid_fields & ~WVM_VCPU_CONTEXT_KNOWN_FIELDS) == 0;
}

size_t wvm_x86_context_legacy_bytes(uint16_t backend)
{
    switch (backend) {
    case WVM_VCPU_BACKEND_KVM:
        return sizeof(wvm_kvm_context_t);
    case WVM_VCPU_BACKEND_TCG:
        return sizeof(wvm_tcg_context_t);
    default:
        return 0;
    }
}

int wvm_x86_context_encode(uint16_t backend, uint64_t valid_fields,
                           const void *legacy_context,
                           size_t legacy_context_bytes, uint8_t *output,
                           size_t output_capacity, size_t *output_bytes,
                           char *error, size_t error_len)
{
    const size_t expected_bytes = wvm_x86_context_legacy_bytes(backend);
    size_t required_bytes;

    if (expected_bytes == 0 || !fields_valid(valid_fields) ||
        !legacy_context || legacy_context_bytes != expected_bytes || !output ||
        !output_bytes || expected_bytes > UINT32_MAX ||
        output_capacity < WVM_X86_CONTEXT_WIRE_HEADER_BYTES + expected_bytes) {
        set_error(error, error_len, "x86 context encode input is invalid");
        return -1;
    }
    required_bytes = WVM_X86_CONTEXT_WIRE_HEADER_BYTES + expected_bytes;
    memset(output, 0, WVM_X86_CONTEXT_WIRE_HEADER_BYTES);
    write_be32(output + CONTEXT_MAGIC_OFFSET, WVM_X86_CONTEXT_MAGIC);
    write_be16(output + CONTEXT_SCHEMA_OFFSET, WVM_X86_CONTEXT_SCHEMA_VERSION);
    write_be16(output + CONTEXT_BACKEND_OFFSET, backend);
    write_be64(output + CONTEXT_FIELDS_OFFSET, valid_fields);
    write_be32(output + CONTEXT_BODY_BYTES_OFFSET, (uint32_t)expected_bytes);
    memcpy(output + WVM_X86_CONTEXT_WIRE_HEADER_BYTES, legacy_context,
           expected_bytes);
    *output_bytes = required_bytes;
    return 0;
}

int wvm_x86_context_validate(uint16_t expected_backend,
                             const uint8_t *input, size_t input_bytes,
                             uint64_t *valid_fields_out, char *error,
                             size_t error_len)
{
    const size_t expected_bytes =
        wvm_x86_context_legacy_bytes(expected_backend);
    uint16_t backend;
    uint64_t valid_fields;
    uint32_t body_bytes;

    if (expected_bytes == 0 || !input || !valid_fields_out ||
        input_bytes < WVM_X86_CONTEXT_WIRE_HEADER_BYTES ||
        read_be32(input + CONTEXT_MAGIC_OFFSET) != WVM_X86_CONTEXT_MAGIC ||
        read_be16(input + CONTEXT_SCHEMA_OFFSET) !=
            WVM_X86_CONTEXT_SCHEMA_VERSION ||
        !all_zero(input + CONTEXT_RESERVED_OFFSET, 4)) {
        set_error(error, error_len, "x86 context header is invalid");
        return -1;
    }
    backend = read_be16(input + CONTEXT_BACKEND_OFFSET);
    valid_fields = read_be64(input + CONTEXT_FIELDS_OFFSET);
    body_bytes = read_be32(input + CONTEXT_BODY_BYTES_OFFSET);
    if (backend != expected_backend || !fields_valid(valid_fields) ||
        body_bytes != expected_bytes ||
        input_bytes != WVM_X86_CONTEXT_WIRE_HEADER_BYTES + expected_bytes) {
        set_error(error, error_len, "x86 context profile does not match");
        return -1;
    }
    *valid_fields_out = valid_fields;
    return 0;
}

int wvm_x86_context_decode(uint16_t expected_backend,
                           const uint8_t *input, size_t input_bytes,
                           uint64_t *valid_fields_out, void *legacy_context,
                           size_t legacy_context_capacity, char *error,
                           size_t error_len)
{
    const size_t expected_bytes =
        wvm_x86_context_legacy_bytes(expected_backend);

    if (!legacy_context || legacy_context_capacity < expected_bytes ||
        wvm_x86_context_validate(expected_backend, input, input_bytes,
                                 valid_fields_out, error, error_len) != 0) {
        if (!error || error[0] == '\0') {
            set_error(error, error_len, "x86 context decode output is invalid");
        }
        return -1;
    }
    memcpy(legacy_context, input + WVM_X86_CONTEXT_WIRE_HEADER_BYTES,
           expected_bytes);
    return 0;
}
