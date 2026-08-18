#include <stdio.h>
#include <string.h>

#include "wavevm_canonical.h"
#include "wavevm_manifest.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "VM request test: %s\n", message);
        return -1;
    }
    return 0;
}

int main(void)
{
    struct wvm_host_constraint constraints[1];
    struct wvm_vm_request request;
    struct wvm_host_constraint decoded_constraints[1];
    struct wvm_vm_request decoded;
    struct wvm_canonical_record record;
    struct wvm_canonical_field field;
    uint8_t bytes[4096];
    size_t encoded_bytes;
    size_t offset;
    int found_lifecycle = 0;
    char error[256] = {0};

    memset(constraints, 0, sizeof(constraints));
    constraints[0].constraint_kind = WVM_MANIFEST_HOST_CONSTRAINT_LABEL;
    constraints[0].comparison_operator = WVM_MANIFEST_HOST_CONSTRAINT_EQUALS;
    strcpy(constraints[0].subject, "zone");
    strcpy(constraints[0].value, "trusted-a");

    memset(&request, 0, sizeof(request));
    request.api_version = WVM_CANONICAL_SCHEMA;
    request.request_id[WVM_IDENTITY_ID_BYTES - 1] = 0x42;
    request.has_display_name = 1;
    strcpy(request.display_name, "tcg-control-plane-contract");
    request.requested_vcpus = 4;
    request.requested_memory_bytes = 8 * 1024 * 1024;
    request.execution_backend_policy = WVM_MANIFEST_BACKEND_POLICY_REQUIRE_TCG;
    request.accelerator_policy = WVM_MANIFEST_ACCELERATOR_DISABLED;
    request.placement_policy = WVM_MANIFEST_PLACEMENT_SPREAD;
    request.host_constraints.entries = constraints;
    request.host_constraints.count = 1;
    request.host_constraints.capacity = 1;
    request.guest_topology_policy = WVM_MANIFEST_GUEST_TOPOLOGY_FLAT;
    request.consistency_policy.dirty_batch_size = 1;
    request.consistency_policy.handoff_commit_policy = 1;
    request.consistency_policy.subscriber_delivery_policy = 1;
    request.consistency_policy.max_commit_latency_ms = 1000;
    memset(request.storage_device_plan.qemu_device_configuration_digest, 0x71,
           sizeof(request.storage_device_plan.qemu_device_configuration_digest));
    request.lifecycle_policy.start_policy = 1;
    request.lifecycle_policy.failure_policy = 1;
    request.lifecycle_policy.completion_query_horizon_ms = 5000;
    request.lifecycle_policy.route_retention_horizon_ms = 6000;

    if (expect(wvm_vm_request_encode(&request, bytes, sizeof(bytes),
                                     &encoded_bytes, error, sizeof(error)) == 0,
               "encode canonical VM request")) {
        return 1;
    }

    memset(&decoded, 0, sizeof(decoded));
    decoded.host_constraints.entries = decoded_constraints;
    decoded.host_constraints.capacity = 1;
    if (expect(wvm_vm_request_decode(bytes, encoded_bytes, &decoded, error,
                                     sizeof(error)) == 0,
               "decode canonical VM request") ||
        expect(decoded.requested_vcpus == request.requested_vcpus &&
                   decoded.requested_memory_bytes == request.requested_memory_bytes,
               "preserve requested resource shape") ||
        expect(decoded.host_constraints.count == 1 &&
                   strcmp(decoded.host_constraints.entries[0].subject, "zone") == 0,
               "preserve sorted host constraint") ||
        expect(decoded.execution_backend_policy ==
                   WVM_MANIFEST_BACKEND_POLICY_REQUIRE_TCG,
               "preserve backend policy")) {
        return 1;
    }

    if (expect(wvm_canonical_record_parse(bytes, encoded_bytes, &record) == 0,
               "parse encoded VM request")) {
        return 1;
    }
    offset = 0;
    while (wvm_canonical_record_next(&record, &offset, &field) == 1) {
        if (field.tag == 13) {
            ((uint8_t *)field.value)[1] ^= 0x01;
            found_lifecycle = 1;
            break;
        }
    }
    if (expect(found_lifecycle, "locate nested lifecycle policy")) {
        return 1;
    }
    if (expect(wvm_vm_request_decode(bytes, encoded_bytes, &decoded, error,
                                     sizeof(error)) != 0,
               "reject malformed nested lifecycle policy")) {
        return 1;
    }

    puts("VM request tests: PASS");
    return 0;
}
