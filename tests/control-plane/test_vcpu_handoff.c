#include <stdio.h>
#include <string.h>

#include "wavevm_vcpu_handoff.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "vCPU handoff test: %s\n", message);
        return -1;
    }
    return 0;
}

static void fill_envelope(const struct wvm_vcpu_handoff_request *request,
                          struct wvm_envelope *envelope,
                          const uint8_t *payload, size_t payload_bytes)
{
    memset(envelope, 0, sizeof(*envelope));
    envelope->message_type = WVM_ENVELOPE_MSG_VCPU_RUN;
    envelope->vm_id = request->vm_id;
    envelope->vm_incarnation = request->vm_incarnation;
    envelope->manifest_generation = request->manifest_generation;
    envelope->origin_physical_node_id = request->origin_physical_node_id;
    envelope->origin_runtime_instance_id = request->origin_runtime_instance_id;
    memcpy(envelope->operation_id, request->operation_id,
           sizeof(envelope->operation_id));
    envelope->route.destination_kind =
        WVM_ENVELOPE_ROUTE_DESTINATION_FLAT_VNODE;
    envelope->route.destination_vnode_or_endpoint =
        (uint32_t)request->destination_executor_id;
    envelope->payload = payload;
    envelope->payload_bytes = payload_bytes;
}

int main(void)
{
    struct wvm_vcpu_handoff_request input;
    struct wvm_vcpu_handoff_request output;
    struct wvm_vcpu_handoff_result result;
    struct wvm_vcpu_handoff_result decoded_result;
    struct wvm_envelope envelope;
    struct wvm_envelope response;
    struct wvm_envelope_route reply_route;
    uint8_t context[48];
    uint8_t result_context[48];
    uint8_t encoded[WVM_VCPU_HANDOFF_REQUEST_HEADER_BYTES + sizeof(context)];
    uint8_t encoded_result[WVM_VCPU_HANDOFF_RESULT_HEADER_BYTES +
                           sizeof(result_context)];
    size_t encoded_bytes = 0;
    size_t encoded_result_bytes = 0;
    char error[256] = {0};

    memset(context, 0xa5, sizeof(context));
    memset(result_context, 0x5a, sizeof(result_context));
    memset(&input, 0, sizeof(input));
    input.protocol_version = WVM_VCPU_HANDOFF_REQUEST_VERSION;
    input.backend = WVM_VCPU_BACKEND_KVM;
    input.context_schema_version = WVM_VCPU_CONTEXT_SCHEMA_X86;
    input.memory_fence_result = WVM_VCPU_MEMORY_FENCE_SUCCEEDED;
    input.vm_id = 9;
    input.vm_incarnation = 77;
    input.manifest_generation = 3;
    input.origin_physical_node_id = 12;
    input.origin_runtime_instance_id = 123;
    input.vcpu_index = 4;
    input.destination_executor_id = 16;
    input.reply_destination_kind =
        WVM_ENVELOPE_ROUTE_DESTINATION_FLAT_VNODE;
    input.reply_destination_vnode = 7;
    input.handoff_sequence = 21;
    input.memory_fence_id = 34;
    input.local_interrupt_watermark = 55;
    input.device_event_watermark = 89;
    input.operation_id[15] = 1;
    input.context_valid_fields =
        WVM_VCPU_CONTEXT_FIELD_ARCHITECTURAL_STATE |
        WVM_VCPU_CONTEXT_FIELD_INTERRUPT_STATE |
        WVM_VCPU_CONTEXT_FIELD_TIMER_STATE;
    input.context = context;
    input.context_bytes = sizeof(context);

    if (expect(wvm_vcpu_handoff_request_encode(
                   &input, encoded, sizeof(encoded), &encoded_bytes, error,
                   sizeof(error)) == 0,
               "encode") ||
        expect(wvm_vcpu_handoff_request_decode(
                   encoded, encoded_bytes, &output, error,
                   sizeof(error)) == 0,
               "decode") ||
        expect(output.backend == input.backend, "backend") ||
        expect(output.vcpu_index == input.vcpu_index, "vCPU index") ||
        expect(output.handoff_sequence == input.handoff_sequence,
               "handoff sequence") ||
        expect(output.reply_destination_kind == input.reply_destination_kind &&
                   output.reply_destination_scope ==
                       input.reply_destination_scope &&
                   output.reply_destination_vnode ==
                       input.reply_destination_vnode,
               "reply destination") ||
        expect(output.context_bytes == sizeof(context), "context bytes") ||
        expect(memcmp(output.context, context, sizeof(context)) == 0,
               "context payload")) {
        return 1;
    }

    fill_envelope(&input, &envelope, encoded, encoded_bytes);
    if (expect(wvm_vcpu_handoff_request_validate_envelope(
                   &output, &envelope, error, sizeof(error)) == 0,
               "validate matching envelope")) {
        return 1;
    }
    memset(&result, 0, sizeof(result));
    result.protocol_version = WVM_VCPU_HANDOFF_RESULT_VERSION;
    result.status = WVM_VCPU_HANDOFF_RESULT_SUCCESS;
    result.exit_class = WVM_VCPU_EXIT_BUDGET;
    result.backend = input.backend;
    result.vm_id = input.vm_id;
    result.vm_incarnation = input.vm_incarnation;
    result.manifest_generation = input.manifest_generation;
    result.origin_physical_node_id = input.origin_physical_node_id;
    result.origin_runtime_instance_id = input.origin_runtime_instance_id;
    result.vcpu_index = input.vcpu_index;
    result.handoff_sequence = input.handoff_sequence;
    result.produced_memory_fence_id = input.memory_fence_id + 1U;
    result.remote_interrupt_watermark = input.local_interrupt_watermark + 1U;
    result.remote_device_watermark = input.device_event_watermark + 1U;
    memcpy(result.operation_id, input.operation_id, sizeof(result.operation_id));
    result.context_schema_version = WVM_VCPU_CONTEXT_SCHEMA_X86;
    result.context_valid_fields =
        WVM_VCPU_CONTEXT_FIELD_ARCHITECTURAL_STATE |
        WVM_VCPU_CONTEXT_FIELD_INTERRUPT_STATE;
    result.context = result_context;
    result.context_bytes = sizeof(result_context);
    memset(&reply_route, 0, sizeof(reply_route));
    reply_route.destination_kind = input.reply_destination_kind;
    reply_route.destination_scope = input.reply_destination_scope;
    reply_route.destination_vnode_or_endpoint = input.reply_destination_vnode;
    reply_route.hop_limit = 4;
    if (expect(wvm_vcpu_handoff_exit_envelope_build(
                   &envelope, &output, &reply_route, 2, &result,
                   encoded_result, sizeof(encoded_result),
                   &encoded_result_bytes, &response, error,
                   sizeof(error)) == 0 &&
                   response.message_type == WVM_ENVELOPE_MSG_VCPU_EXIT &&
                   response.route.destination_kind ==
                       input.reply_destination_kind &&
                   response.route.destination_scope ==
                       input.reply_destination_scope &&
                   response.route.destination_vnode_or_endpoint ==
                       input.reply_destination_vnode &&
                   wvm_vcpu_handoff_result_decode(
                       response.payload, response.payload_bytes,
                       &decoded_result, error, sizeof(error)) == 0 &&
                   wvm_vcpu_handoff_exit_validate_envelope(
                       &output, &decoded_result, &response, error,
                       sizeof(error)) == 0,
               "build typed exit for explicit reply route")) {
        return 1;
    }
    response.route.destination_vnode_or_endpoint++;
    if (expect(wvm_vcpu_handoff_exit_validate_envelope(
                   &output, &decoded_result, &response, error,
                   sizeof(error)) != 0,
               "reject exit targeting another reply route")) {
        return 1;
    }
    response.route.destination_vnode_or_endpoint--;
    input.context_valid_fields |= 1ULL << 63;
    if (expect(wvm_vcpu_handoff_request_encode(
                   &input, encoded, sizeof(encoded), &encoded_bytes, error,
                   sizeof(error)) != 0,
               "reject unknown context field")) {
        return 1;
    }
    input.context_valid_fields &= ~(1ULL << 63);
    input.reply_destination_scope = 1;
    if (expect(wvm_vcpu_handoff_request_encode(
                   &input, encoded, sizeof(encoded), &encoded_bytes, error,
                   sizeof(error)) != 0,
               "reject invalid flat reply scope")) {
        return 1;
    }
    input.reply_destination_scope = 0;
    envelope.route.destination_vnode_or_endpoint++;
    if (expect(wvm_vcpu_handoff_request_validate_envelope(
                   &output, &envelope, error, sizeof(error)) != 0,
               "reject another destination executor")) {
        return 1;
    }
    envelope.route.destination_vnode_or_endpoint--;
    encoded[WVM_VCPU_HANDOFF_REQUEST_HEADER_BYTES] ^= 0xff;
    if (expect(wvm_vcpu_handoff_request_decode(
                   encoded, encoded_bytes, &output, error,
                   sizeof(error)) != 0,
               "reject context digest mismatch")) {
        return 1;
    }

    puts("vCPU handoff tests: PASS");
    return 0;
}
