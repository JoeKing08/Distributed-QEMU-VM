#include <stdio.h>
#include <string.h>

#include "wavevm_envelope.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "envelope test: %s\n", message);
        return -1;
    }
    return 0;
}

struct emitted_frames {
    uint8_t frames[8][WVM_ENVELOPE_MAX_NETWORK_FRAME_BYTES];
    size_t frame_bytes[8];
    size_t count;
};

static int collect_frame(void *opaque, const uint8_t *frame,
                         size_t frame_bytes, char *error, size_t error_len)
{
    struct emitted_frames *frames = opaque;

    (void)error;
    (void)error_len;
    if (!frames || !frame || frame_bytes == 0 ||
        frame_bytes > sizeof(frames->frames[0]) ||
        frames->count >= sizeof(frames->frames) / sizeof(frames->frames[0])) {
        return -1;
    }
    memcpy(frames->frames[frames->count], frame, frame_bytes);
    frames->frame_bytes[frames->count] = frame_bytes;
    frames->count++;
    return 0;
}

static void fill_routed_envelope(struct wvm_envelope *envelope,
                                 const uint8_t *payload, size_t payload_bytes)
{
    memset(envelope, 0, sizeof(*envelope));
    envelope->message_type = WVM_ENVELOPE_MSG_MEM_READ;
    envelope->vm_id = 256;
    envelope->vm_incarnation = 17;
    envelope->manifest_generation = 3;
    envelope->origin_physical_node_id = 9;
    envelope->origin_runtime_instance_id = 44;
    envelope->operation_id[15] = 1;
    envelope->delivery_attempt_id = 1;
    envelope->route_scope_id = 51;
    envelope->topology_revision = 7;
    envelope->route_generation = 12;
    memset(envelope->route_snapshot_digest, 0x5a,
           sizeof(envelope->route_snapshot_digest));
    envelope->route.destination_kind =
        WVM_ENVELOPE_ROUTE_DESTINATION_FLAT_VNODE;
    envelope->route.destination_vnode_or_endpoint = 77;
    envelope->route.hop_limit = 4;
    envelope->payload = payload;
    envelope->payload_bytes = payload_bytes;
}

static void fill_fragment(uint8_t *fragment, const uint8_t *logical,
                          size_t logical_bytes, uint16_t index,
                          uint16_t count, uint32_t offset, size_t data_bytes)
{
    memset(fragment, 0, WVM_ENVELOPE_FRAGMENT_PREFIX_BYTES + data_bytes);
    fragment[0] = (uint8_t)(logical_bytes >> 24);
    fragment[1] = (uint8_t)(logical_bytes >> 16);
    fragment[2] = (uint8_t)(logical_bytes >> 8);
    fragment[3] = (uint8_t)logical_bytes;
    fragment[4] = (uint8_t)(index >> 8);
    fragment[5] = (uint8_t)index;
    fragment[6] = (uint8_t)(count >> 8);
    fragment[7] = (uint8_t)count;
    fragment[8] = (uint8_t)(offset >> 24);
    fragment[9] = (uint8_t)(offset >> 16);
    fragment[10] = (uint8_t)(offset >> 8);
    fragment[11] = (uint8_t)offset;
    memcpy(fragment + WVM_ENVELOPE_FRAGMENT_PREFIX_BYTES, logical + offset,
           data_bytes);
}

int main(void)
{
    struct wvm_envelope input;
    struct wvm_envelope decoded;
    struct wvm_envelope_admitted_identity admitted;
    struct wvm_envelope_reassembler reassembler;
    struct wvm_envelope_reassembled completed;
    uint8_t payload[] = {0x10, 0x20, 0x30, 0x40};
    uint8_t oversized[WVM_ENVELOPE_MAX_NETWORK_FRAME_BYTES];
    uint8_t encoded[WVM_ENVELOPE_HEADER_BYTES +
                    WVM_ENVELOPE_ROUTE_PREFIX_BYTES + sizeof(payload)];
    uint8_t logical[1500];
    uint8_t memory_ack_logical[32 + 4096];
    uint8_t fragment_zero[WVM_ENVELOPE_FRAGMENT_PREFIX_BYTES + 1024];
    uint8_t fragment_one[WVM_ENVELOPE_FRAGMENT_PREFIX_BYTES + 476];
    uint8_t encoded_fragment[
        WVM_ENVELOPE_MAX_NETWORK_FRAME_BYTES];
    size_t encoded_bytes = 0;
    char error[256] = {0};
    size_t i;

    fill_routed_envelope(&input, payload, sizeof(payload));
    if (expect(wvm_envelope_crc32c((const uint8_t *)"123456789", 9) ==
                   0xe3069283U,
               "CRC32C Castagnoli vector") ||
        expect(wvm_envelope_encode(
                   &input, WVM_ENVELOPE_TRANSPORT_NETWORK, encoded,
                   sizeof(encoded), &encoded_bytes, error, sizeof(error)) == 0,
               "encode routed envelope") ||
        expect(encoded_bytes == sizeof(encoded),
               "encoded size") ||
        expect(encoded[0] == 'W' && encoded[1] == 'V' && encoded[2] == 'M' &&
                   encoded[3] == '1',
               "wire magic") ||
        expect(encoded[16] == 0 && encoded[17] == 0 &&
                   encoded[18] == 1 && encoded[19] == 0,
               "u32 VM ID wire encoding") ||
        expect(wvm_envelope_decode(
                   encoded, encoded_bytes, WVM_ENVELOPE_TRANSPORT_NETWORK,
                   &decoded, error, sizeof(error)) == 0,
               "decode routed envelope") ||
        expect(decoded.vm_id == input.vm_id &&
                   decoded.vm_incarnation == input.vm_incarnation &&
                   decoded.route.destination_kind ==
                       WVM_ENVELOPE_ROUTE_DESTINATION_FLAT_VNODE &&
                   decoded.route.destination_scope == 0 &&
                   decoded.route.destination_vnode_or_endpoint == 77 &&
                   decoded.route.hop_limit == 4 &&
                   decoded.payload_bytes == sizeof(payload) &&
                   memcmp(decoded.payload, payload, sizeof(payload)) == 0,
               "round-trip envelope identity/payload")) {
        return 1;
    }

    memset(&admitted, 0, sizeof(admitted));
    admitted.vm_id = input.vm_id;
    admitted.vm_incarnation = input.vm_incarnation;
    admitted.manifest_generation = input.manifest_generation;
    admitted.route_scope_id = input.route_scope_id;
    admitted.topology_revision = input.topology_revision;
    admitted.route_generation = input.route_generation;
    memcpy(admitted.route_snapshot_digest, input.route_snapshot_digest,
           sizeof(admitted.route_snapshot_digest));
    if (expect(wvm_envelope_validate_admitted(&decoded, &admitted, error,
                                                 sizeof(error)) == 0,
               "admitted identity") ||
        expect((admitted.vm_incarnation++,
                wvm_envelope_validate_admitted(
                    &decoded, &admitted, error, sizeof(error)) != 0),
               "reject stale incarnation")) {
        return 1;
    }
    admitted.vm_incarnation--;

    input.route.destination_kind =
        WVM_ENVELOPE_ROUTE_DESTINATION_FRACTAL_VNODE;
    input.route.destination_scope = 0x1234;
    if (expect(wvm_envelope_encode(
                   &input, WVM_ENVELOPE_TRANSPORT_NETWORK, encoded,
                   sizeof(encoded), &encoded_bytes, error, sizeof(error)) == 0 &&
                   wvm_envelope_decode(
                       encoded, encoded_bytes,
                       WVM_ENVELOPE_TRANSPORT_NETWORK, &decoded, error,
                       sizeof(error)) == 0 &&
                   decoded.route.destination_scope == 0x1234 &&
                   wvm_envelope_route_advance(&decoded, error,
                                                  sizeof(error)) == 0 &&
                   decoded.route.hop_count == 1,
               "fractal route prefix and hop advancement") ||
        expect((decoded.route.hop_count = decoded.route.hop_limit,
                wvm_envelope_route_advance(&decoded, error,
                                               sizeof(error)) != 0),
               "reject exhausted route hop budget")) {
        return 1;
    }
    input.route.destination_kind =
        WVM_ENVELOPE_ROUTE_DESTINATION_FLAT_VNODE;
    input.route.destination_scope = 0;

    input.flags = 0x8000U;
    if (expect(wvm_envelope_encode(
                   &input, WVM_ENVELOPE_TRANSPORT_NETWORK, encoded,
                   sizeof(encoded), &encoded_bytes, error, sizeof(error)) != 0,
               "reject unknown flag")) {
        return 1;
    }
    input.flags = 0;
    input.message_type = 0x7fffU;
    if (expect(wvm_envelope_encode(
                   &input, WVM_ENVELOPE_TRANSPORT_NETWORK, encoded,
                   sizeof(encoded), &encoded_bytes, error, sizeof(error)) != 0,
               "reject unknown message type")) {
        return 1;
    }
    input.message_type = WVM_ENVELOPE_MSG_MEM_READ;
    memset(oversized, 0, sizeof(oversized));
    input.payload = oversized;
    input.payload_bytes = sizeof(oversized);
    if (expect(wvm_envelope_encode(
                   &input, WVM_ENVELOPE_TRANSPORT_NETWORK, encoded_fragment,
                   sizeof(encoded_fragment), &encoded_bytes, error,
                   sizeof(error)) != 0,
               "reject oversized network frame")) {
        return 1;
    }
    input.payload = payload;
    input.payload_bytes = sizeof(payload);

    encoded[4] = 0;
    encoded[5] = 2;
    if (expect(wvm_envelope_decode(
                   encoded, encoded_bytes, WVM_ENVELOPE_TRANSPORT_NETWORK,
                   &decoded, error, sizeof(error)) != 0,
               "reject unknown protocol version")) {
        return 1;
    }
    if (expect(wvm_envelope_encode(
                   &input, WVM_ENVELOPE_TRANSPORT_NETWORK, encoded,
                   sizeof(encoded), &encoded_bytes, error, sizeof(error)) == 0,
               "re-encode baseline") ||
        expect(wvm_envelope_decode(
                   encoded, encoded_bytes - 1,
                   WVM_ENVELOPE_TRANSPORT_NETWORK, &decoded, error,
                   sizeof(error)) != 0,
               "reject truncated frame")) {
        return 1;
    }
    if (expect(wvm_envelope_encode(
                   &input, WVM_ENVELOPE_TRANSPORT_NETWORK, encoded,
                   sizeof(encoded), &encoded_bytes, error, sizeof(error)) == 0,
               "re-encode CRC baseline")) {
        return 1;
    }
    encoded[WVM_ENVELOPE_HEADER_BYTES] ^= 0xff;
    if (expect(wvm_envelope_decode(
                   encoded, encoded_bytes, WVM_ENVELOPE_TRANSPORT_NETWORK,
                   &decoded, error, sizeof(error)) != 0,
               "reject bad CRC")) {
        return 1;
    }

    for (i = 0; i < sizeof(logical); i++) {
        logical[i] = (uint8_t)i;
    }
    fill_fragment(fragment_zero, logical, sizeof(logical), 0, 2, 0, 1024);
    fill_fragment(fragment_one, logical, sizeof(logical), 1, 2, 1024, 476);
    fill_routed_envelope(&input, fragment_zero, sizeof(fragment_zero));
    input.flags = WVM_ENVELOPE_FLAG_FRAGMENTED;
    wvm_envelope_semantic_digest(logical, sizeof(logical),
                                    input.semantic_payload_digest);
    fragment_zero[12] = 1;
    if (expect(wvm_envelope_encode(
                   &input, WVM_ENVELOPE_TRANSPORT_NETWORK, encoded_fragment,
                   sizeof(encoded_fragment), &encoded_bytes, error,
                   sizeof(error)) != 0,
               "reject nonzero fragment reserved byte")) {
        return 1;
    }
    fragment_zero[12] = 0;
    if (expect(wvm_envelope_reassembler_init(&reassembler) == 0,
               "initialize reassembler") ||
        expect(wvm_envelope_encode(
                   &input, WVM_ENVELOPE_TRANSPORT_NETWORK, encoded_fragment,
                   sizeof(encoded_fragment), &encoded_bytes, error,
                   sizeof(error)) == 0 &&
                   wvm_envelope_decode(
                       encoded_fragment, encoded_bytes,
                       WVM_ENVELOPE_TRANSPORT_NETWORK, &decoded, error,
                       sizeof(error)) == 0 &&
                   wvm_envelope_reassembler_accept(
                       &reassembler, &decoded, 100, &completed, error,
                       sizeof(error)) == 0,
               "accept first fragment") ||
        expect(wvm_envelope_reassembler_accept(
                   &reassembler, &decoded, 101, &completed, error,
                   sizeof(error)) == 0,
               "accept identical fragment retry")) {
        wvm_envelope_reassembler_destroy(&reassembler);
        return 1;
    }
    input.payload = fragment_one;
    input.payload_bytes = sizeof(fragment_one);
    if (expect(wvm_envelope_encode(
                   &input, WVM_ENVELOPE_TRANSPORT_NETWORK, encoded_fragment,
                   sizeof(encoded_fragment), &encoded_bytes, error,
                   sizeof(error)) == 0 &&
                   wvm_envelope_decode(
                       encoded_fragment, encoded_bytes,
                       WVM_ENVELOPE_TRANSPORT_NETWORK, &decoded, error,
                       sizeof(error)) == 0 &&
                   wvm_envelope_reassembler_accept(
                       &reassembler, &decoded, 102, &completed, error,
                       sizeof(error)) == 1,
               "complete fragmented payload") ||
        expect(completed.payload_bytes == sizeof(logical) &&
                   memcmp(completed.payload, logical, sizeof(logical)) == 0 &&
                   !(completed.envelope.flags &
                     WVM_ENVELOPE_FLAG_FRAGMENTED),
               "reassembled semantic payload")) {
        wvm_envelope_reassembled_release(&completed);
        wvm_envelope_reassembler_destroy(&reassembler);
        return 1;
    }
    wvm_envelope_reassembled_release(&completed);
    if (expect(reassembler.allocated_payload_bytes == 0,
               "completed reassembly releases payload budget")) {
        wvm_envelope_reassembler_destroy(&reassembler);
        return 1;
    }
    wvm_envelope_reassembler_destroy(&reassembler);

    for (i = 0; i < sizeof(memory_ack_logical); i++) {
        memory_ack_logical[i] = (uint8_t)(i * 13U);
    }
    {
        struct emitted_frames emitted;

        memset(&emitted, 0, sizeof(emitted));
        fill_routed_envelope(&input, memory_ack_logical,
                             sizeof(memory_ack_logical));
        if (expect(wvm_envelope_emit_network_frames(
                       &input, collect_frame, &emitted, error,
                       sizeof(error)) == 0 &&
                       emitted.count == 5,
                   "emit bounded frames for a full memory ACK") ||
            expect(wvm_envelope_reassembler_init(&reassembler) == 0,
                   "initialize emitted-frame reassembler")) {
            return 1;
        }
        for (i = 0; i < emitted.count; i++) {
            if (expect(wvm_envelope_decode(
                           emitted.frames[i], emitted.frame_bytes[i],
                           WVM_ENVELOPE_TRANSPORT_NETWORK, &decoded, error,
                           sizeof(error)) == 0,
                       "decode emitted memory ACK fragment")) {
                wvm_envelope_reassembler_destroy(&reassembler);
                return 1;
            }
            if (i + 1U == emitted.count) {
                if (expect(wvm_envelope_reassembler_accept(
                               &reassembler, &decoded, 200U + i, &completed,
                               error, sizeof(error)) == 1,
                           "complete emitted memory ACK fragments")) {
                    wvm_envelope_reassembler_destroy(&reassembler);
                    return 1;
                }
            } else if (expect(wvm_envelope_reassembler_accept(
                                  &reassembler, &decoded, 200U + i,
                                  &completed, error, sizeof(error)) == 0,
                              "retain emitted memory ACK fragment")) {
                wvm_envelope_reassembler_destroy(&reassembler);
                return 1;
            }
        }
        if (expect(completed.payload_bytes == sizeof(memory_ack_logical) &&
                       memcmp(completed.payload, memory_ack_logical,
                              sizeof(memory_ack_logical)) == 0,
                   "reassemble emitted full memory ACK exactly")) {
            wvm_envelope_reassembled_release(&completed);
            wvm_envelope_reassembler_destroy(&reassembler);
            return 1;
        }
        wvm_envelope_reassembled_release(&completed);
        wvm_envelope_reassembler_destroy(&reassembler);
    }

    puts("envelope tests: PASS");
    return 0;
}
