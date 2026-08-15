#include <stdio.h>
#include <string.h>

#include "wavevm_executor_abi.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "executor-abi test: %s\n", message);
        return -1;
    }
    return 0;
}

int main(void)
{
    struct wvm_executor_abi_frame input;
    struct wvm_executor_abi_frame output;
    struct wvm_executor_abi_frame result;
    struct wvm_executor_abi_frame decoded_result;
    struct wvm_node_runtime_manifest manifest;
    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t encoded[WVM_EXECUTOR_ABI_HEADER_BYTES + sizeof(payload)];
    uint8_t result_encoded[WVM_EXECUTOR_ABI_HEADER_BYTES];
    size_t encoded_bytes = 0;
    size_t request_encoded_bytes = 0;
    char error[256] = {0};

    memset(&input, 0, sizeof(input));
    input.identity.vm_id = 9;
    input.identity.vm_incarnation = 77;
    input.identity.manifest_generation = 3;
    input.identity.local_runtime_instance_id = 123;
    input.identity.operation_id[15] = 1;
    memset(input.identity.candidate_manifest_digest, 0x11,
           sizeof(input.identity.candidate_manifest_digest));
    input.identity.route_snapshot_key.scope_key.vm_id = input.identity.vm_id;
    input.identity.route_snapshot_key.scope_key.vm_incarnation =
        input.identity.vm_incarnation;
    input.identity.route_snapshot_key.scope_key.route_scope_id = 5;
    input.identity.route_snapshot_key.topology_revision = 7;
    input.identity.route_snapshot_key.route_generation = 11;
    memset(input.identity.route_snapshot_key.snapshot_digest, 0x22,
           sizeof(input.identity.route_snapshot_key.snapshot_digest));
    memset(input.identity.activation_fence, 0x33,
           sizeof(input.identity.activation_fence));
    input.message_type = WVM_EXECUTOR_ABI_CPU_RUN;
    input.payload = payload;
    input.payload_bytes = sizeof(payload);

    if (expect(wvm_executor_abi_encode(&input, encoded, sizeof(encoded),
                                       &encoded_bytes, error,
                                       sizeof(error)) == 0,
               "encode") ||
        expect(wvm_executor_abi_decode(encoded, encoded_bytes, &output, error,
                                       sizeof(error)) == 0,
               "decode") ||
        expect(output.message_type == input.message_type, "message type") ||
        expect(output.payload_bytes == sizeof(payload), "payload length") ||
        expect(memcmp(output.payload, payload, sizeof(payload)) == 0,
               "payload bytes")) {
        return 1;
    }
    request_encoded_bytes = encoded_bytes;

    memset(&manifest, 0, sizeof(manifest));
    manifest.vm_id = input.identity.vm_id;
    manifest.vm_incarnation = input.identity.vm_incarnation;
    manifest.manifest_generation = input.identity.manifest_generation;
    memcpy(manifest.candidate_manifest_digest,
           input.identity.candidate_manifest_digest,
           sizeof(manifest.candidate_manifest_digest));
    manifest.required_route_snapshot_key = input.identity.route_snapshot_key;
    manifest.has_activation_fence = 1;
    memcpy(manifest.activation_fence, input.identity.activation_fence,
           sizeof(manifest.activation_fence));
    if (expect(wvm_executor_abi_validate_identity(
                   &output, &manifest,
                   input.identity.local_runtime_instance_id, error,
                   sizeof(error)) == 0,
               "identity validation") ||
        expect(wvm_executor_abi_validate_identity(
                   &output, &manifest, 999, error, sizeof(error)) != 0,
               "reject stale runtime identity")) {
        return 1;
    }

    output.identity.candidate_manifest_digest[0] ^= 0xff;
    if (expect(wvm_executor_abi_validate_identity(
                   &output, &manifest,
                   input.identity.local_runtime_instance_id, error,
                   sizeof(error)) != 0,
               "reject stale candidate digest")) {
        return 1;
    }
    output.identity.candidate_manifest_digest[0] ^= 0xff;
    output.identity.route_snapshot_key.snapshot_digest[0] ^= 0xff;
    if (expect(wvm_executor_abi_validate_identity(
                   &output, &manifest,
                   input.identity.local_runtime_instance_id, error,
                   sizeof(error)) != 0,
               "reject stale route snapshot")) {
        return 1;
    }
    output.identity.route_snapshot_key.snapshot_digest[0] ^= 0xff;
    output.identity.activation_fence[0] ^= 0xff;
    if (expect(wvm_executor_abi_validate_identity(
                   &output, &manifest,
                   input.identity.local_runtime_instance_id, error,
                   sizeof(error)) != 0,
               "reject stale activation fence")) {
        return 1;
    }
    output.identity.activation_fence[0] ^= 0xff;

    memset(&result, 0, sizeof(result));
    result.identity = input.identity;
    result.message_type = WVM_EXECUTOR_ABI_RESULT;
    result.status = WVM_EXECUTOR_ABI_SUCCESS;
    if (expect(wvm_executor_abi_encode(&result, result_encoded,
                                       sizeof(result_encoded), &encoded_bytes,
                                       error, sizeof(error)) == 0 &&
                   wvm_executor_abi_decode(result_encoded, encoded_bytes,
                                           &decoded_result, error,
                                           sizeof(error)) == 0 &&
                   wvm_executor_abi_validate_result(
                       &input, &decoded_result, error, sizeof(error)) == 0,
               "validate matching result")) {
        return 1;
    }
    decoded_result.identity.operation_id[0] ^= 0xff;
    if (expect(wvm_executor_abi_validate_result(
                   &input, &decoded_result, error, sizeof(error)) != 0,
               "reject result from another operation")) {
        return 1;
    }
    decoded_result.identity.operation_id[0] ^= 0xff;
    decoded_result.identity.route_snapshot_key.snapshot_digest[0] ^= 0xff;
    if (expect(wvm_executor_abi_validate_result(
                   &input, &decoded_result, error, sizeof(error)) != 0,
               "reject result with another route snapshot")) {
        return 1;
    }

    encoded[WVM_EXECUTOR_ABI_HEADER_BYTES] ^= 0xff;
    if (expect(wvm_executor_abi_decode(encoded, request_encoded_bytes, &output,
                                       error,
                                       sizeof(error)) != 0,
               "reject payload digest mismatch")) {
        return 1;
    }

    puts("executor-abi tests: PASS");
    return 0;
}
