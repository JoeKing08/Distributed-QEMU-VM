#include <stdio.h>
#include <string.h>

#include "wavevm_admission_evidence.h"
#include "wavevm_canonical.h"
#include "wavevm_admission_orchestrator.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "admission-evidence-owner test: %s\n", message);
        return -1;
    }
    return 0;
}

static void fill_capability(struct wvm_capability_record *record,
                            uint16_t capability_id)
{
    memset(record, 0, sizeof(*record));
    record->capability_id = capability_id;
    record->capability_schema_version = WVM_CANONICAL_SCHEMA;
    record->physical_node_id = 17;
    record->node_instance_id = 101;
    record->provider_instance_id = 2000 + capability_id;
    record->state = WVM_CAPABILITY_AVAILABLE;
    record->abi_version = 1;
    record->limits.entries = NULL;
    record->limits.count = 0;
    record->limits.capacity = 0;
    record->constraints.entries = NULL;
    record->constraints.count = 0;
    record->constraints.capacity = 0;
    record->observed_at = 1000 + capability_id;
    record->probe_operation_id[WVM_IDENTITY_ID_BYTES - 1] =
        (uint8_t)capability_id;
}

int main(void)
{
    struct wvm_capability_record source[2];
    struct wvm_capability_record owner_capabilities[2];
    struct wvm_resource_reservation owner_reservations[1];
    struct wvm_admission_evidence_owner owner;
    struct wvm_coordinator_membership_evidence evidence;
    struct wvm_admission_orchestrator_input input;
    struct wvm_vm_request request;
    struct wvm_coordinator_transaction transaction;
    char error[256] = {0};

    fill_capability(&source[0], WVM_CAPABILITY_ID_EXECUTION_KVM);
    fill_capability(&source[1], WVM_CAPABILITY_ID_EXECUTION_TCG);
    if (expect(wvm_admission_evidence_owner_init(
                   &owner, owner_capabilities, 2, owner_reservations, 1,
                   error, sizeof(error)) == 0,
               "initialize bounded evidence owner") ||
        expect(wvm_admission_evidence_owner_publish(
                   &owner, source, 2, NULL, 0, 7, 9, error, sizeof(error)) ==
                   0,
               "publish validated evidence") ||
        expect(wvm_admission_evidence_owner_capture(&owner, &evidence, error,
                                                    sizeof(error)) == 0,
               "capture immutable evidence view") ||
        expect(evidence.capability_record_count == 2 &&
                   evidence.resource_reservation_count == 0 &&
                   evidence.inventory_revision == 7 &&
                   evidence.capability_profile_generation == 9,
               "capture preserves evidence revisions") ||
        expect(evidence.capability_records == owner_capabilities,
               "capture points at owner storage") ||
        expect(wvm_admission_evidence_owner_refresh_input(
                   &owner, WVM_ADMISSION_INPUT_PREPARE, &request, &transaction,
                   &input, error, sizeof(error)) == 0 &&
                   input.membership_evidence == &owner.evidence_view,
               "refresh adapter binds the owner evidence view") ||
        expect(wvm_admission_evidence_owner_publish(
                   &owner, source, 2, NULL, 0, 8, 10, error, sizeof(error)) !=
                   0,
               "reject replacement after publication") ||
        expect(wvm_admission_evidence_owner_validate(&owner, error,
                                                     sizeof(error)) == 0,
               "validate retained publication")) {
        return 1;
    }

    source[0].capability_id = WVM_CAPABILITY_ID_EXECUTION_TCG;
    source[1].capability_id = WVM_CAPABILITY_ID_EXECUTION_KVM;
    source[0].provider_instance_id = 2002;
    source[1].provider_instance_id = 2001;
    memset(&owner, 0, sizeof(owner));
    if (expect(wvm_admission_evidence_owner_init(
                   &owner, owner_capabilities, 2, owner_reservations, 1,
                   error, sizeof(error)) == 0,
               "initialize owner for invalid publication") ||
        expect(wvm_admission_evidence_owner_publish(
                   &owner, source, 2, NULL, 0, 7, 9, error, sizeof(error)) !=
                   0,
               "reject unsorted capability evidence")) {
        return 1;
    }

    puts("admission-evidence-owner tests: PASS");
    return 0;
}
