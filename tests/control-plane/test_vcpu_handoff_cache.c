#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "wavevm_vcpu_handoff_cache.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "vCPU handoff cache test: %s\n", message);
        return -1;
    }
    return 0;
}

static int encode_request(struct wvm_vcpu_handoff_request *request,
                          struct wvm_envelope *envelope, uint8_t *payload,
                          size_t payload_capacity, size_t *payload_bytes,
                          char *error, size_t error_len)
{
    if (wvm_vcpu_handoff_request_encode(request, payload, payload_capacity,
                                        payload_bytes, error, error_len) !=
        0) {
        return -1;
    }
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
    envelope->payload_bytes = *payload_bytes;
    wvm_envelope_semantic_digest(payload, *payload_bytes,
                                 envelope->semantic_payload_digest);
    return 0;
}

static void fill_result(const struct wvm_vcpu_handoff_request *request,
                        const uint8_t *context, size_t context_bytes,
                        struct wvm_vcpu_handoff_result *result)
{
    memset(result, 0, sizeof(*result));
    result->protocol_version = WVM_VCPU_HANDOFF_RESULT_VERSION;
    result->status = WVM_VCPU_HANDOFF_RESULT_SUCCESS;
    result->exit_class = WVM_VCPU_EXIT_BUDGET;
    result->backend = request->backend;
    result->vm_id = request->vm_id;
    result->vm_incarnation = request->vm_incarnation;
    result->manifest_generation = request->manifest_generation;
    result->origin_physical_node_id = request->origin_physical_node_id;
    result->origin_runtime_instance_id = request->origin_runtime_instance_id;
    result->vcpu_index = request->vcpu_index;
    result->handoff_sequence = request->handoff_sequence;
    result->produced_memory_fence_id = request->memory_fence_id + 1U;
    result->remote_interrupt_watermark =
        request->local_interrupt_watermark + 1U;
    result->remote_device_watermark = request->device_event_watermark + 1U;
    memcpy(result->operation_id, request->operation_id,
           sizeof(result->operation_id));
    result->context_schema_version = WVM_VCPU_CONTEXT_SCHEMA_X86;
    result->context_valid_fields =
        WVM_VCPU_CONTEXT_FIELD_ARCHITECTURAL_STATE |
        WVM_VCPU_CONTEXT_FIELD_INTERRUPT_STATE;
    result->context = context;
    result->context_bytes = context_bytes;
}

int main(void)
{
    struct wvm_vcpu_handoff_cache cache;
    struct wvm_vcpu_handoff_request request;
    struct wvm_vcpu_handoff_result result;
    struct wvm_vcpu_handoff_result replayed_result;
    struct wvm_envelope envelope;
    enum wvm_vcpu_handoff_cache_decision decision;
    uint8_t request_context[32];
    uint8_t result_context[32];
    uint8_t payload[WVM_VCPU_HANDOFF_REQUEST_HEADER_BYTES +
                    sizeof(request_context)];
    uint8_t replay[WVM_VCPU_HANDOFF_RESULT_HEADER_BYTES +
                   sizeof(result_context)];
    size_t payload_bytes = 0;
    size_t replay_bytes = 0;
    char error[256] = {0};

    memset(request_context, 0x11, sizeof(request_context));
    memset(result_context, 0x22, sizeof(result_context));
    memset(&request, 0, sizeof(request));
    request.protocol_version = WVM_VCPU_HANDOFF_REQUEST_VERSION;
    request.backend = WVM_VCPU_BACKEND_TCG;
    request.context_schema_version = WVM_VCPU_CONTEXT_SCHEMA_X86;
    request.memory_fence_result = WVM_VCPU_MEMORY_FENCE_SUCCEEDED;
    request.vm_id = 9;
    request.vm_incarnation = 11;
    request.manifest_generation = 13;
    request.origin_physical_node_id = 17;
    request.origin_runtime_instance_id = 19;
    request.vcpu_index = 3;
    request.destination_executor_id = 23;
    request.reply_destination_kind =
        WVM_ENVELOPE_ROUTE_DESTINATION_FLAT_VNODE;
    request.reply_destination_vnode = 29;
    request.handoff_sequence = 1;
    request.memory_fence_id = 29;
    request.local_interrupt_watermark = 31;
    request.device_event_watermark = 37;
    request.operation_id[15] = 1;
    request.context_valid_fields =
        WVM_VCPU_CONTEXT_FIELD_ARCHITECTURAL_STATE |
        WVM_VCPU_CONTEXT_FIELD_INTERRUPT_STATE;
    request.context = request_context;
    request.context_bytes = sizeof(request_context);

    if (expect(wvm_vcpu_handoff_cache_init(
                   &cache, 2, 10, sizeof(replay), error, sizeof(error)) == 0,
               "initialize cache") ||
        expect(encode_request(&request, &envelope, payload, sizeof(payload),
                              &payload_bytes, error, sizeof(error)) == 0,
               "encode first request") ||
        expect(wvm_vcpu_handoff_cache_begin(
                   &cache, &request, &envelope, 1, &decision, replay,
                   sizeof(replay), &replay_bytes, error, sizeof(error)) == 0 &&
                   decision == WVM_VCPU_HANDOFF_CACHE_EXECUTE &&
                   replay_bytes == 0,
               "admit first request for one execution") ||
        expect(wvm_vcpu_handoff_cache_begin(
                   &cache, &request, &envelope, 2, &decision, replay,
                   sizeof(replay), &replay_bytes, error, sizeof(error)) == 0 &&
                   decision == WVM_VCPU_HANDOFF_CACHE_IN_PROGRESS &&
                   replay_bytes == 0,
               "detect duplicate while execution is in progress")) {
        wvm_vcpu_handoff_cache_destroy(&cache);
        return 1;
    }

    request.operation_id[15] = 2;
    if (expect(encode_request(&request, &envelope, payload, sizeof(payload),
                              &payload_bytes, error, sizeof(error)) == 0 &&
                   wvm_vcpu_handoff_cache_begin(
                       &cache, &request, &envelope, 3, &decision, replay,
                       sizeof(replay), &replay_bytes, error,
                       sizeof(error)) == -EPROTO,
               "reject another operation for the same vCPU sequence")) {
        wvm_vcpu_handoff_cache_destroy(&cache);
        return 1;
    }

    request.operation_id[15] = 1;
    if (expect(encode_request(&request, &envelope, payload, sizeof(payload),
                              &payload_bytes, error, sizeof(error)) == 0,
               "restore first request") ||
        expect((fill_result(&request, result_context, sizeof(result_context),
                            &result),
                wvm_vcpu_handoff_cache_complete(
                    &cache, &request, &envelope, &result, 4, error,
                    sizeof(error)) == 0),
               "complete first request") ||
        expect(wvm_vcpu_handoff_cache_begin(
                   &cache, &request, &envelope, 5, &decision, replay,
                   sizeof(replay), &replay_bytes, error, sizeof(error)) == 0 &&
                   decision == WVM_VCPU_HANDOFF_CACHE_REPLAY &&
                   wvm_vcpu_handoff_result_decode(
                       replay, replay_bytes, &replayed_result, error,
                       sizeof(error)) == 0 &&
                   wvm_vcpu_handoff_result_validate_request(
                       &request, &replayed_result, error, sizeof(error)) == 0,
               "replay exact typed completion without execution")) {
        wvm_vcpu_handoff_cache_destroy(&cache);
        return 1;
    }

    wvm_vcpu_handoff_cache_prune(&cache, 14);
    if (expect(wvm_vcpu_handoff_cache_begin(
                   &cache, &request, &envelope, 14, &decision, replay,
                   sizeof(replay), &replay_bytes, error, sizeof(error)) == 0 &&
                   decision == WVM_VCPU_HANDOFF_CACHE_RESULT_EXPIRED,
               "expired result never becomes executable again")) {
        wvm_vcpu_handoff_cache_destroy(&cache);
        return 1;
    }

    request.handoff_sequence = 2;
    request.operation_id[15] = 3;
    if (expect(encode_request(&request, &envelope, payload, sizeof(payload),
                              &payload_bytes, error, sizeof(error)) == 0 &&
                   wvm_vcpu_handoff_cache_begin(
                       &cache, &request, &envelope, 15, &decision, replay,
                       sizeof(replay), &replay_bytes, error,
                       sizeof(error)) == 0 &&
                   decision == WVM_VCPU_HANDOFF_CACHE_EXECUTE,
               "admit the next ordered vCPU interval")) {
        wvm_vcpu_handoff_cache_destroy(&cache);
        return 1;
    }

    request.handoff_sequence = 4;
    request.operation_id[15] = 4;
    if (expect(encode_request(&request, &envelope, payload, sizeof(payload),
                              &payload_bytes, error, sizeof(error)) == 0 &&
                   wvm_vcpu_handoff_cache_begin(
                       &cache, &request, &envelope, 16, &decision, replay,
                       sizeof(replay), &replay_bytes, error,
                       sizeof(error)) == -EALREADY,
               "reject later interval while one vCPU handoff is active")) {
        wvm_vcpu_handoff_cache_destroy(&cache);
        return 1;
    }

    request.handoff_sequence = 2;
    request.operation_id[15] = 3;
    if (expect(encode_request(&request, &envelope, payload, sizeof(payload),
                              &payload_bytes, error, sizeof(error)) == 0 &&
                   (fill_result(&request, result_context, sizeof(result_context),
                                &result),
                    wvm_vcpu_handoff_cache_complete(
                        &cache, &request, &envelope, &result, 17, error,
                        sizeof(error)) == 0),
               "complete second request")) {
        wvm_vcpu_handoff_cache_destroy(&cache);
        return 1;
    }

    request.handoff_sequence = 4;
    request.operation_id[15] = 4;
    if (expect(encode_request(&request, &envelope, payload, sizeof(payload),
                              &payload_bytes, error, sizeof(error)) == 0 &&
                   wvm_vcpu_handoff_cache_begin(
                       &cache, &request, &envelope, 18, &decision, replay,
                       sizeof(replay), &replay_bytes, error,
                       sizeof(error)) == -EPROTO,
               "reject skipped handoff sequence")) {
        wvm_vcpu_handoff_cache_destroy(&cache);
        return 1;
    }

    wvm_vcpu_handoff_cache_destroy(&cache);
    puts("vCPU handoff cache tests: PASS");
    return 0;
}
