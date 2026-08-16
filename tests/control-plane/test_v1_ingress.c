#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "wavevm_memory_v1.h"
#include "v1_ingress.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "V1 ingress test: %s\n", message);
        return -1;
    }
    return 0;
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static int setup_manifest(struct wvm_node_runtime_manifest *manifest,
                          struct wvm_capability_ref *capability, char *error,
                          size_t error_len)
{
    struct wvm_local_name_identity name_identity;

    memset(manifest, 0, sizeof(*manifest));
    memset(manifest->candidate_manifest_digest, 0x11,
           sizeof(manifest->candidate_manifest_digest));
    manifest->vm_id = 256;
    manifest->vm_incarnation = 4;
    manifest->manifest_generation = 1;
    memset(manifest->admission_tx_id, 0x22, sizeof(manifest->admission_tx_id));
    memset(manifest->eligibility_fence_digest, 0x33,
           sizeof(manifest->eligibility_fence_digest));
    manifest->has_activation_fence = 1;
    memset(manifest->activation_fence, 0x44,
           sizeof(manifest->activation_fence));
    manifest->physical_node_id = 17;
    manifest->expected_node_instance_id = 101;
    manifest->local_role_bits =
        WVM_RUNTIME_ROLE_BIT(WVM_MANIFEST_ROLE_NODE_RUNTIME) |
        WVM_RUNTIME_ROLE_BIT(WVM_MANIFEST_ROLE_EXECUTOR);
    manifest->required_route_snapshot_key.scope_key.vm_id = manifest->vm_id;
    manifest->required_route_snapshot_key.scope_key.vm_incarnation =
        manifest->vm_incarnation;
    manifest->required_route_snapshot_key.scope_key.route_scope_id = 9;
    manifest->required_route_snapshot_key.topology_revision = 2;
    manifest->required_route_snapshot_key.route_generation = 3;
    memset(manifest->required_route_snapshot_key.snapshot_digest, 0x55,
           sizeof(manifest->required_route_snapshot_key.snapshot_digest));
    memset(&name_identity, 0, sizeof(name_identity));
    name_identity.vm_id = manifest->vm_id;
    name_identity.vm_incarnation = manifest->vm_incarnation;
    name_identity.manifest_generation = manifest->manifest_generation;
    name_identity.physical_node_id = manifest->physical_node_id;
    memset(name_identity.manifest_id, 0x66, sizeof(name_identity.manifest_id));
    memcpy(name_identity.admission_tx_id, manifest->admission_tx_id,
           sizeof(name_identity.admission_tx_id));
    if (wvm_local_name_namespace_derive(&name_identity, &manifest->local_names,
                                        error, error_len) != 0) {
        return -1;
    }
    manifest->negotiated_profile.backend = WVM_MANIFEST_BACKEND_TCG;
    manifest->negotiated_profile.context_schema_version = 1;
    manifest->negotiated_profile.dirty_capture_engine = 1;
    manifest->negotiated_profile.read_fault_engine = 1;
    manifest->negotiated_profile.invalidation_engine = 1;
    manifest->negotiated_profile.fallback_decision = 1;
    memset(manifest->negotiated_profile.supported_memory_policies_digest, 0x77,
           sizeof(manifest->negotiated_profile.supported_memory_policies_digest));
    memset(capability, 0, sizeof(*capability));
    capability->physical_node_id = manifest->physical_node_id;
    capability->node_instance_id = manifest->expected_node_instance_id;
    capability->profile_generation = 8;
    memset(capability->profile_digest, 0x88,
           sizeof(capability->profile_digest));
    manifest->negotiated_profile.per_node_capabilities.entries = capability;
    manifest->negotiated_profile.per_node_capabilities.count = 1;
    manifest->negotiated_profile.per_node_capabilities.capacity = 1;
    memset(manifest->reservation_id, 0x99, sizeof(manifest->reservation_id));
    return wvm_node_runtime_manifest_validate(manifest, error, error_len);
}

static void fill_envelope(const struct wvm_node_runtime_manifest *manifest,
                          struct wvm_envelope_v1 *envelope,
                          const uint8_t *payload, size_t payload_bytes)
{
    memset(envelope, 0, sizeof(*envelope));
    envelope->message_type = WVM_ENVELOPE_V1_MSG_MEM_READ;
    envelope->vm_id = manifest->vm_id;
    envelope->vm_incarnation = manifest->vm_incarnation;
    envelope->manifest_generation = manifest->manifest_generation;
    envelope->origin_physical_node_id = 18;
    envelope->origin_runtime_instance_id = 202;
    envelope->operation_id[15] = 1;
    envelope->delivery_attempt_id = 1;
    envelope->route_scope_id =
        manifest->required_route_snapshot_key.scope_key.route_scope_id;
    envelope->topology_revision =
        manifest->required_route_snapshot_key.topology_revision;
    envelope->route_generation =
        manifest->required_route_snapshot_key.route_generation;
    memcpy(envelope->route_snapshot_digest,
           manifest->required_route_snapshot_key.snapshot_digest,
           sizeof(envelope->route_snapshot_digest));
    envelope->route.destination_kind =
        WVM_ENVELOPE_V1_ROUTE_DESTINATION_FLAT_VNODE;
    envelope->route.destination_vnode_or_endpoint = 12;
    envelope->route.hop_limit = 4;
    envelope->payload = payload;
    envelope->payload_bytes = payload_bytes;
}

static int recording_dispatch(void *opaque,
                              const struct wvm_envelope_v1 *envelope,
                              char *error, size_t error_len)
{
    int *count = opaque;

    (void)error;
    (void)error_len;
    if (!count || !envelope ||
        (envelope->message_type != WVM_ENVELOPE_V1_MSG_MEM_READ &&
         envelope->message_type != WVM_ENVELOPE_V1_MSG_MEM_ACK &&
         envelope->message_type != WVM_ENVELOPE_V1_MSG_VCPU_RUN)) {
        return -EINVAL;
    }
    (*count)++;
    return 0;
}

static int unsupported_dispatch(void *opaque,
                                const struct wvm_envelope_v1 *envelope,
                                char *error, size_t error_len)
{
    (void)opaque;
    (void)envelope;
    if (error && error_len != 0) {
        snprintf(error, error_len, "typed schema is absent");
    }
    return -ENOTSUP;
}

static void fill_fragment(uint8_t *fragment, const uint8_t *logical,
                          size_t logical_bytes, uint16_t index,
                          uint16_t count, uint32_t offset, size_t data_bytes)
{
    memset(fragment, 0, WVM_ENVELOPE_V1_FRAGMENT_PREFIX_BYTES + data_bytes);
    write_be32(fragment, (uint32_t)logical_bytes);
    write_be16(fragment + 4, index);
    write_be16(fragment + 6, count);
    write_be32(fragment + 8, offset);
    memcpy(fragment + WVM_ENVELOPE_V1_FRAGMENT_PREFIX_BYTES, logical + offset,
           data_bytes);
}

int main(void)
{
    struct wvm_node_runtime_manifest manifest;
    struct wvm_capability_ref capability;
    struct wvm_runtime_gate gate;
    struct wvm_runtime_registration registration;
    struct wvm_v1_ingress_config config;
    struct wvm_v1_ingress ingress;
    struct wvm_envelope_v1_reassembler reassembler;
    struct wvm_envelope_v1 envelope;
    struct wvm_v1_mem_read memory_read;
    struct wvm_v1_mem_ack memory_ack;
    uint8_t payload[WVM_V1_MEM_READ_PAYLOAD_BYTES];
    uint8_t frame[WVM_ENVELOPE_V1_MAX_NETWORK_FRAME_BYTES];
    uint8_t logical[WVM_V1_MEM_ACK_HEADER_BYTES +
                    WVM_V1_MEMORY_PAGE_BYTES];
    uint8_t fragment[WVM_ENVELOPE_V1_FRAGMENT_PREFIX_BYTES +
                     WVM_ENVELOPE_V1_MAX_FRAGMENT_DATA_BYTES];
    uint8_t ack_page[WVM_V1_MEMORY_PAGE_BYTES];
    uint8_t profile_digest[WVM_SHA256_DIGEST_BYTES];
    size_t frame_bytes = 0;
    size_t logical_bytes = 0;
    size_t fragment_data_bytes;
    size_t fragment_count;
    size_t offset;
    uint64_t connection_id = 0;
    int dispatched = 0;
    int fragmented_ack_accepted = 1;
    char error[256] = {0};
    size_t i;

    if (expect(setup_manifest(&manifest, &capability, error, sizeof(error)) ==
                   0,
               "build admitted manifest") ||
        expect(wvm_runtime_manifest_profile_digest(
                   &manifest, profile_digest, error, sizeof(error)) == 0,
               "derive profile digest")) {
        return 1;
    }
    wvm_runtime_gate_init(&gate);
    if (expect(wvm_runtime_gate_prepare(&gate, &manifest, 17, 101, error,
                                        sizeof(error)) == 0,
               "prepare runtime gate")) {
        return 1;
    }
    memset(&registration, 0, sizeof(registration));
    registration.connection_role = WVM_MANIFEST_ROLE_NODE_RUNTIME;
    registration.vm_id = manifest.vm_id;
    registration.vm_incarnation = manifest.vm_incarnation;
    registration.manifest_generation = manifest.manifest_generation;
    memcpy(registration.candidate_manifest_digest,
           manifest.candidate_manifest_digest,
           sizeof(registration.candidate_manifest_digest));
    registration.local_runtime_instance_id = manifest.expected_node_instance_id;
    registration.caller_process_instance_id = 501;
    memcpy(registration.capability_profile_digest, profile_digest,
           sizeof(registration.capability_profile_digest));
    snprintf(registration.requested_endpoint_name,
             sizeof(registration.requested_endpoint_name), "%s",
             manifest.local_names.namespace_name);
    if (expect(wvm_runtime_gate_register(&gate, &registration, &connection_id,
                                         error, sizeof(error)) == 0 &&
                   wvm_runtime_gate_activate(&gate, manifest.activation_fence,
                                             error, sizeof(error)) == 0,
               "register and activate node runtime")) {
        return 1;
    }

    memset(&config, 0, sizeof(config));
    config.manifest = &manifest;
    config.runtime_gate = &gate;
    config.runtime_connection_id = connection_id;
    config.dispatch = recording_dispatch;
    config.dispatch_opaque = &dispatched;
    if (expect(wvm_v1_ingress_init(&ingress, &config, error, sizeof(error)) ==
                   0 &&
                   wvm_envelope_v1_reassembler_init(&reassembler) == 0,
               "initialize ingress and bounded reassembler")) {
        return 1;
    }

    memset(&memory_read, 0, sizeof(memory_read));
    memory_read.gpa = 2 * WVM_V1_MEMORY_PAGE_BYTES;
    memory_read.reply_destination_kind =
        WVM_ENVELOPE_V1_ROUTE_DESTINATION_FLAT_VNODE;
    memory_read.reply_destination_vnode = 3;
    if (expect(wvm_v1_mem_read_encode(&memory_read, payload, error,
                                      sizeof(error)) == 0,
               "encode typed memory read payload")) {
        wvm_envelope_v1_reassembler_destroy(&reassembler);
        return 1;
    }
    fill_envelope(&manifest, &envelope, payload, sizeof(payload));
    if (expect(wvm_envelope_v1_encode(&envelope,
                                      WVM_ENVELOPE_V1_TRANSPORT_NETWORK, frame,
                                      sizeof(frame), &frame_bytes, error,
                                      sizeof(error)) == 0 &&
                   wvm_v1_ingress_accept(&ingress, &reassembler, frame,
                                         frame_bytes, 100, error,
                                         sizeof(error)) ==
                       WVM_V1_INGRESS_ACCEPTED &&
                   dispatched == 1,
               "authorize and dispatch admitted V1 operation")) {
        wvm_envelope_v1_reassembler_destroy(&reassembler);
        return 1;
    }

    envelope.route_generation++;
    if (expect(wvm_envelope_v1_encode(&envelope,
                                      WVM_ENVELOPE_V1_TRANSPORT_NETWORK, frame,
                                      sizeof(frame), &frame_bytes, error,
                                      sizeof(error)) == 0 &&
                   wvm_v1_ingress_accept(&ingress, &reassembler, frame,
                                         frame_bytes, 101, error,
                                         sizeof(error)) ==
                       WVM_V1_INGRESS_REJECTED &&
                   dispatched == 1,
               "reject stale route before dispatch")) {
        wvm_envelope_v1_reassembler_destroy(&reassembler);
        return 1;
    }
    envelope.route_generation--;

    payload[10] = 1;
    if (expect(wvm_envelope_v1_encode(&envelope,
                                      WVM_ENVELOPE_V1_TRANSPORT_NETWORK, frame,
                                      sizeof(frame), &frame_bytes, error,
                                      sizeof(error)) == 0 &&
                   wvm_v1_ingress_accept(&ingress, &reassembler, frame,
                                         frame_bytes, 102, error,
                                         sizeof(error)) ==
                       WVM_V1_INGRESS_REJECTED &&
                   dispatched == 1,
               "reject malformed typed memory payload before dispatch")) {
        wvm_envelope_v1_reassembler_destroy(&reassembler);
        return 1;
    }
    payload[10] = 0;

    memset(ack_page, 0xa5, sizeof(ack_page));
    memset(&memory_ack, 0, sizeof(memory_ack));
    memory_ack.gpa = 2 * WVM_V1_MEMORY_PAGE_BYTES;
    memory_ack.version = 5;
    memory_ack.status = WVM_V1_MEM_ACK_SUCCESS;
    memory_ack.directory_physical_node_id = 18;
    memory_ack.directory_node_instance_id = 202;
    memory_ack.data = ack_page;
    memory_ack.data_bytes = sizeof(ack_page);
    if (expect(wvm_v1_mem_ack_encode(&memory_ack, logical, sizeof(logical),
                                     &logical_bytes, error,
                                     sizeof(error)) == 0,
               "encode a full-page typed memory ACK for fragmentation")) {
        wvm_envelope_v1_reassembler_destroy(&reassembler);
        return 1;
    }
    fragment_count =
        (logical_bytes + WVM_ENVELOPE_V1_MAX_FRAGMENT_DATA_BYTES - 1U) /
        WVM_ENVELOPE_V1_MAX_FRAGMENT_DATA_BYTES;
    envelope.message_type = WVM_ENVELOPE_V1_MSG_MEM_ACK;
    envelope.flags = WVM_ENVELOPE_V1_FLAG_FRAGMENTED;
    envelope.operation_id[15] = 2;
    wvm_envelope_v1_semantic_digest(logical, logical_bytes,
                                    envelope.semantic_payload_digest);
    for (i = 0, offset = 0; i < fragment_count; i++) {
        fragment_data_bytes = logical_bytes - offset;
        if (fragment_data_bytes > WVM_ENVELOPE_V1_MAX_FRAGMENT_DATA_BYTES) {
            fragment_data_bytes = WVM_ENVELOPE_V1_MAX_FRAGMENT_DATA_BYTES;
        }
        fill_fragment(fragment, logical, logical_bytes, (uint16_t)i,
                      (uint16_t)fragment_count, (uint32_t)offset,
                      fragment_data_bytes);
        envelope.payload = fragment;
        envelope.payload_bytes =
            WVM_ENVELOPE_V1_FRAGMENT_PREFIX_BYTES + fragment_data_bytes;
        if (wvm_envelope_v1_encode(
                &envelope, WVM_ENVELOPE_V1_TRANSPORT_NETWORK, frame,
                sizeof(frame), &frame_bytes, error, sizeof(error)) != 0 ||
            wvm_v1_ingress_accept(
                &ingress, &reassembler, frame, frame_bytes, 102 + i, error,
                sizeof(error)) !=
                (i + 1U == fragment_count ? WVM_V1_INGRESS_ACCEPTED
                                           : WVM_V1_INGRESS_INCOMPLETE)) {
            fragmented_ack_accepted = 0;
            break;
        }
        offset += fragment_data_bytes;
    }
    if (expect(fragmented_ack_accepted && dispatched == 2,
               "reassemble one fragmented full-page ACK into one dispatch")) {
        wvm_envelope_v1_reassembler_destroy(&reassembler);
        return 1;
    }

    config.dispatch = unsupported_dispatch;
    if (expect(wvm_v1_ingress_init(&ingress, &config, error, sizeof(error)) ==
                   0,
               "reconfigure test ingress with typed rejection")) {
        wvm_envelope_v1_reassembler_destroy(&reassembler);
        return 1;
    }
    envelope.flags = 0;
    envelope.message_type = WVM_ENVELOPE_V1_MSG_MEM_READ;
    envelope.operation_id[15] = 3;
    envelope.payload = payload;
    envelope.payload_bytes = sizeof(payload);
    if (expect(wvm_envelope_v1_encode(&envelope,
                                      WVM_ENVELOPE_V1_TRANSPORT_NETWORK, frame,
                                      sizeof(frame), &frame_bytes, error,
                                      sizeof(error)) == 0 &&
                   wvm_v1_ingress_accept(&ingress, &reassembler, frame,
                                         frame_bytes, 104, error,
                                         sizeof(error)) ==
                       WVM_V1_INGRESS_UNSUPPORTED,
               "typed unsupported semantic payload")) {
        wvm_envelope_v1_reassembler_destroy(&reassembler);
        return 1;
    }
    if (expect(wvm_runtime_gate_revoke(&gate, connection_id, error,
                                       sizeof(error)) == 0 &&
                   wvm_v1_ingress_accept(&ingress, &reassembler, frame,
                                         frame_bytes, 105, error,
                                         sizeof(error)) ==
                       WVM_V1_INGRESS_REJECTED,
               "reject after runtime gate revocation")) {
        wvm_envelope_v1_reassembler_destroy(&reassembler);
        return 1;
    }
    wvm_envelope_v1_reassembler_destroy(&reassembler);
    puts("V1 ingress tests: PASS");
    return 0;
}
