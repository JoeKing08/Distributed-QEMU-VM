#include "wavevm_executor_abi.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "wavevm_protocol.h"
#include "wavevm_sha256.h"

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

static int all_zero(const uint8_t *bytes, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        if (bytes[i] != 0) {
            return 0;
        }
    }
    return 1;
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

int wvm_executor_abi_encode(
    const struct wvm_executor_abi_frame *frame, uint8_t *output,
    size_t capacity, size_t *encoded_bytes, char *error, size_t error_len)
{
    uint8_t payload_digest[WVM_SHA256_DIGEST_BYTES];

    if (!frame || !output || !encoded_bytes ||
        frame->message_type == 0 ||
        frame->payload_bytes > WVM_EXECUTOR_ABI_MAX_PAYLOAD ||
        (frame->payload_bytes != 0 && !frame->payload) ||
        frame->identity.vm_id == 0 ||
        frame->identity.vm_incarnation == 0 ||
        frame->identity.manifest_generation == 0 ||
        frame->identity.local_runtime_instance_id == 0 ||
        all_zero(frame->identity.operation_id,
                 sizeof(frame->identity.operation_id)) ||
        all_zero(frame->identity.candidate_manifest_digest,
                 sizeof(frame->identity.candidate_manifest_digest)) ||
        wvm_route_snapshot_key_validate(
            &frame->identity.route_snapshot_key, error, error_len) != 0 ||
        frame->identity.route_snapshot_key.scope_key.vm_id !=
            frame->identity.vm_id ||
        frame->identity.route_snapshot_key.scope_key.vm_incarnation !=
            frame->identity.vm_incarnation ||
        all_zero(frame->identity.activation_fence,
                 sizeof(frame->identity.activation_fence)) ||
        capacity < WVM_EXECUTOR_ABI_HEADER_BYTES + frame->payload_bytes) {
        set_error(error, error_len, "executor ABI frame input is invalid");
        return -1;
    }

    memset(output, 0, WVM_EXECUTOR_ABI_HEADER_BYTES);
    write_be32(output + 0, WVM_EXECUTOR_ABI_MAGIC);
    write_be16(output + 4, WVM_EXECUTOR_ABI_VERSION);
    write_be16(output + 6, WVM_EXECUTOR_ABI_HEADER_BYTES);
    write_be16(output + 8, frame->message_type);
    write_be16(output + 10, frame->status);
    write_be32(output + 12, (uint32_t)frame->payload_bytes);
    write_be32(output + 16, frame->identity.vm_id);
    write_be64(output + 20, frame->identity.vm_incarnation);
    write_be64(output + 28, frame->identity.manifest_generation);
    write_be64(output + 36,
               frame->identity.route_snapshot_key.route_generation);
    write_be64(output + 44, frame->identity.local_runtime_instance_id);
    memcpy(output + 52, frame->identity.operation_id,
           sizeof(frame->identity.operation_id));
    memcpy(output + 100, frame->identity.candidate_manifest_digest,
           sizeof(frame->identity.candidate_manifest_digest));
    write_be64(output + 132,
               frame->identity.route_snapshot_key.scope_key.route_scope_id);
    write_be64(output + 140, frame->identity.route_snapshot_key.topology_revision);
    memcpy(output + 148, frame->identity.route_snapshot_key.snapshot_digest,
           sizeof(frame->identity.route_snapshot_key.snapshot_digest));
    memcpy(output + 180, frame->identity.activation_fence,
           sizeof(frame->identity.activation_fence));
    if (frame->payload_bytes != 0) {
        wvm_sha256_digest(frame->payload, frame->payload_bytes, payload_digest);
        memcpy(output + 68, payload_digest, sizeof(payload_digest));
        memcpy(output + WVM_EXECUTOR_ABI_HEADER_BYTES, frame->payload,
               frame->payload_bytes);
    }
    *encoded_bytes = WVM_EXECUTOR_ABI_HEADER_BYTES + frame->payload_bytes;
    return 0;
}

int wvm_executor_abi_decode(
    const uint8_t *input, size_t input_bytes,
    struct wvm_executor_abi_frame *frame, char *error, size_t error_len)
{
    uint8_t expected_digest[WVM_SHA256_DIGEST_BYTES];
    uint32_t payload_bytes;

    if (!input || !frame || input_bytes < WVM_EXECUTOR_ABI_HEADER_BYTES ||
        read_be32(input + 0) != WVM_EXECUTOR_ABI_MAGIC ||
        read_be16(input + 4) != WVM_EXECUTOR_ABI_VERSION ||
        read_be16(input + 6) != WVM_EXECUTOR_ABI_HEADER_BYTES) {
        set_error(error, error_len, "executor ABI header is invalid");
        return -1;
    }
    payload_bytes = read_be32(input + 12);
    if (payload_bytes > WVM_EXECUTOR_ABI_MAX_PAYLOAD ||
        input_bytes != WVM_EXECUTOR_ABI_HEADER_BYTES + payload_bytes ||
        read_be16(input + 8) == 0 ||
        read_be32(input + 16) == 0 ||
        read_be64(input + 20) == 0 ||
        read_be64(input + 28) == 0 ||
        read_be64(input + 36) == 0 ||
        read_be64(input + 44) == 0 ||
        all_zero(input + 52, 16)) {
        set_error(error, error_len, "executor ABI frame bounds/identity invalid");
        return -1;
    }
    if (payload_bytes != 0) {
        wvm_sha256_digest(input + WVM_EXECUTOR_ABI_HEADER_BYTES, payload_bytes,
                          expected_digest);
        if (memcmp(expected_digest, input + 68,
                   sizeof(expected_digest)) != 0) {
            set_error(error, error_len, "executor ABI payload digest mismatch");
            return -1;
        }
    } else if (!all_zero(input + 68, WVM_SHA256_DIGEST_BYTES)) {
        set_error(error, error_len, "empty executor ABI payload has digest");
        return -1;
    }

    memset(frame, 0, sizeof(*frame));
    frame->message_type = read_be16(input + 8);
    frame->status = read_be16(input + 10);
    frame->identity.vm_id = read_be32(input + 16);
    frame->identity.vm_incarnation = read_be64(input + 20);
    frame->identity.manifest_generation = read_be64(input + 28);
    frame->identity.local_runtime_instance_id = read_be64(input + 44);
    memcpy(frame->identity.operation_id, input + 52,
           sizeof(frame->identity.operation_id));
    frame->identity.route_snapshot_key.scope_key.vm_id =
        frame->identity.vm_id;
    frame->identity.route_snapshot_key.scope_key.vm_incarnation =
        frame->identity.vm_incarnation;
    frame->identity.route_snapshot_key.route_generation = read_be64(input + 36);
    memcpy(frame->identity.candidate_manifest_digest, input + 100,
           sizeof(frame->identity.candidate_manifest_digest));
    frame->identity.route_snapshot_key.scope_key.route_scope_id =
        read_be64(input + 132);
    frame->identity.route_snapshot_key.topology_revision = read_be64(input + 140);
    memcpy(frame->identity.route_snapshot_key.snapshot_digest, input + 148,
           sizeof(frame->identity.route_snapshot_key.snapshot_digest));
    memcpy(frame->identity.activation_fence, input + 180,
           sizeof(frame->identity.activation_fence));
    if (all_zero(frame->identity.candidate_manifest_digest,
                 sizeof(frame->identity.candidate_manifest_digest)) ||
        wvm_route_snapshot_key_validate(&frame->identity.route_snapshot_key,
                                        error, error_len) != 0 ||
        all_zero(frame->identity.activation_fence,
                 sizeof(frame->identity.activation_fence))) {
        set_error(error, error_len, "executor ABI admitted identity is invalid");
        return -1;
    }
    frame->payload = input + WVM_EXECUTOR_ABI_HEADER_BYTES;
    frame->payload_bytes = payload_bytes;
    return 0;
}

int wvm_executor_abi_validate_identity(
    const struct wvm_executor_abi_frame *frame,
    const struct wvm_node_runtime_manifest *manifest,
    uint64_t local_runtime_instance_id, char *error, size_t error_len)
{
    if (!frame || !manifest ||
        frame->identity.vm_id != manifest->vm_id ||
        frame->identity.vm_incarnation != manifest->vm_incarnation ||
        frame->identity.manifest_generation != manifest->manifest_generation ||
        frame->identity.local_runtime_instance_id != local_runtime_instance_id ||
        memcmp(frame->identity.candidate_manifest_digest,
               manifest->candidate_manifest_digest,
               sizeof(manifest->candidate_manifest_digest)) != 0 ||
        frame->identity.route_snapshot_key.scope_key.route_scope_id !=
            manifest->required_route_snapshot_key.scope_key.route_scope_id ||
        frame->identity.route_snapshot_key.topology_revision !=
            manifest->required_route_snapshot_key.topology_revision ||
        frame->identity.route_snapshot_key.route_generation !=
            manifest->required_route_snapshot_key.route_generation ||
        memcmp(frame->identity.route_snapshot_key.snapshot_digest,
               manifest->required_route_snapshot_key.snapshot_digest,
               sizeof(manifest->required_route_snapshot_key.snapshot_digest)) !=
            0 ||
        !manifest->has_activation_fence ||
        memcmp(frame->identity.activation_fence, manifest->activation_fence,
               sizeof(manifest->activation_fence)) != 0) {
        set_error(error, error_len,
                  "executor ABI identity does not match admitted manifest");
        return -1;
    }
    return 0;
}

int wvm_executor_abi_validate_result(
    const struct wvm_executor_abi_frame *request,
    const struct wvm_executor_abi_frame *result, char *error,
    size_t error_len)
{
    if (!request || !result ||
        request->message_type != WVM_EXECUTOR_ABI_CPU_RUN ||
        result->message_type != WVM_EXECUTOR_ABI_RESULT ||
        result->status > WVM_EXECUTOR_ABI_INTERNAL_FAILURE ||
        result->payload_bytes != 0 ||
        result->identity.vm_id != request->identity.vm_id ||
        result->identity.vm_incarnation != request->identity.vm_incarnation ||
        result->identity.manifest_generation !=
            request->identity.manifest_generation ||
        result->identity.local_runtime_instance_id !=
            request->identity.local_runtime_instance_id ||
        memcmp(result->identity.operation_id, request->identity.operation_id,
               sizeof(result->identity.operation_id)) != 0 ||
        memcmp(result->identity.candidate_manifest_digest,
               request->identity.candidate_manifest_digest,
               sizeof(result->identity.candidate_manifest_digest)) != 0 ||
        result->identity.route_snapshot_key.scope_key.route_scope_id !=
            request->identity.route_snapshot_key.scope_key.route_scope_id ||
        result->identity.route_snapshot_key.topology_revision !=
            request->identity.route_snapshot_key.topology_revision ||
        result->identity.route_snapshot_key.route_generation !=
            request->identity.route_snapshot_key.route_generation ||
        memcmp(result->identity.route_snapshot_key.snapshot_digest,
               request->identity.route_snapshot_key.snapshot_digest,
               sizeof(result->identity.route_snapshot_key.snapshot_digest)) !=
            0 ||
        memcmp(result->identity.activation_fence,
               request->identity.activation_fence,
               sizeof(result->identity.activation_fence)) != 0) {
        set_error(error, error_len,
                  "executor ABI result does not match submitted operation");
        return -1;
    }
    return 0;
}
