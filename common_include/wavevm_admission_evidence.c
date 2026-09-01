#include "wavevm_admission_evidence.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "wavevm_admission_orchestrator.h"

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

static int capability_record_compare(
    const struct wvm_capability_record *left,
    const struct wvm_capability_record *right)
{
    if (left->physical_node_id != right->physical_node_id) {
        return left->physical_node_id < right->physical_node_id ? -1 : 1;
    }
    if (left->node_instance_id != right->node_instance_id) {
        return left->node_instance_id < right->node_instance_id ? -1 : 1;
    }
    if (left->capability_id != right->capability_id) {
        return left->capability_id < right->capability_id ? -1 : 1;
    }
    if (left->provider_instance_id != right->provider_instance_id) {
        return left->provider_instance_id < right->provider_instance_id ? -1 : 1;
    }
    return 0;
}

static int reservation_id_compare(
    const struct wvm_resource_reservation *left,
    const struct wvm_resource_reservation *right)
{
    return memcmp(left->reservation_id, right->reservation_id,
                  sizeof(left->reservation_id));
}

static int evidence_values_validate(
    const struct wvm_capability_record *capability_records,
    size_t capability_record_count,
    const struct wvm_resource_reservation *resource_reservations,
    size_t resource_reservation_count, uint64_t inventory_revision,
    uint64_t capability_profile_generation, char *error, size_t error_len)
{
    size_t i;

    if (!capability_records || capability_record_count == 0 ||
        capability_record_count > UINT32_MAX ||
        (resource_reservation_count != 0 && !resource_reservations) ||
        inventory_revision == 0 || capability_profile_generation == 0) {
        set_error(error, error_len, "admission evidence publication is incomplete");
        return -1;
    }
    for (i = 0; i < capability_record_count; i++) {
        if (wvm_capability_record_validate(&capability_records[i], error,
                                           error_len) != 0 ||
            (i != 0 &&
             capability_record_compare(&capability_records[i - 1],
                                       &capability_records[i]) >= 0)) {
            set_error(error, error_len,
                      "capability evidence is not strictly ordered");
            return -1;
        }
    }
    for (i = 0; i < resource_reservation_count; i++) {
        if (wvm_resource_reservation_validate(&resource_reservations[i], error,
                                              error_len) != 0 ||
            (i != 0 &&
             reservation_id_compare(&resource_reservations[i - 1],
                                    &resource_reservations[i]) >= 0)) {
            set_error(error, error_len,
                      "reservation evidence is not strictly ordered");
            return -1;
        }
    }
    return 0;
}

int wvm_admission_evidence_owner_init(
    struct wvm_admission_evidence_owner *owner,
    struct wvm_capability_record *capability_records,
    size_t capability_record_capacity,
    struct wvm_resource_reservation *resource_reservations,
    size_t resource_reservation_capacity, char *error, size_t error_len)
{
    if (!owner || !capability_records || capability_record_capacity == 0 ||
        (resource_reservation_capacity != 0 && !resource_reservations)) {
        set_error(error, error_len, "admission evidence owner storage is invalid");
        return -1;
    }
    memset(owner, 0, sizeof(*owner));
    owner->capability_records = capability_records;
    owner->capability_record_capacity = capability_record_capacity;
    owner->resource_reservations = resource_reservations;
    owner->resource_reservation_capacity = resource_reservation_capacity;
    owner->initialized = 1;
    return 0;
}

int wvm_admission_evidence_owner_publish(
    struct wvm_admission_evidence_owner *owner,
    const struct wvm_capability_record *capability_records,
    size_t capability_record_count,
    const struct wvm_resource_reservation *resource_reservations,
    size_t resource_reservation_count, uint64_t inventory_revision,
    uint64_t capability_profile_generation, char *error, size_t error_len)
{
    if (!owner || !owner->initialized || owner->published ||
        capability_record_count > owner->capability_record_capacity ||
        resource_reservation_count > owner->resource_reservation_capacity ||
        evidence_values_validate(capability_records, capability_record_count,
                                  resource_reservations,
                                  resource_reservation_count,
                                  inventory_revision,
                                  capability_profile_generation, error,
                                  error_len) != 0) {
        if (error && error[0] == '\0') {
            set_error(error, error_len,
                      "admission evidence owner publication is invalid");
        }
        return -1;
    }
    memmove(owner->capability_records, capability_records,
            capability_record_count * sizeof(*owner->capability_records));
    if (resource_reservation_count != 0) {
        memmove(owner->resource_reservations, resource_reservations,
                resource_reservation_count *
                    sizeof(*owner->resource_reservations));
    }
    owner->capability_record_count = capability_record_count;
    owner->resource_reservation_count = resource_reservation_count;
    owner->inventory_revision = inventory_revision;
    owner->capability_profile_generation = capability_profile_generation;
    owner->published = 1;
    return 0;
}

int wvm_admission_evidence_owner_validate(
    const struct wvm_admission_evidence_owner *owner, char *error,
    size_t error_len)
{
    if (!owner || !owner->initialized || !owner->published ||
        owner->capability_record_count > owner->capability_record_capacity ||
        owner->resource_reservation_count > owner->resource_reservation_capacity ||
        evidence_values_validate(
            owner->capability_records, owner->capability_record_count,
            owner->resource_reservations, owner->resource_reservation_count,
            owner->inventory_revision, owner->capability_profile_generation,
            error, error_len) != 0) {
        if (error && error[0] == '\0') {
            set_error(error, error_len, "admission evidence owner is invalid");
        }
        return -1;
    }
    return 0;
}

int wvm_admission_evidence_owner_capture(
    const struct wvm_admission_evidence_owner *owner,
    struct wvm_coordinator_membership_evidence *evidence, char *error,
    size_t error_len)
{
    if (wvm_admission_evidence_owner_validate(owner, error, error_len) != 0 ||
        !evidence) {
        if (error && error[0] == '\0') {
            set_error(error, error_len, "cannot capture admission evidence");
        }
        return -1;
    }
    memset(evidence, 0, sizeof(*evidence));
    evidence->capability_records = owner->capability_records;
    evidence->capability_record_count = owner->capability_record_count;
    evidence->resource_reservations = owner->resource_reservations;
    evidence->resource_reservation_count = owner->resource_reservation_count;
    evidence->inventory_revision = owner->inventory_revision;
    evidence->capability_profile_generation =
        owner->capability_profile_generation;
    return 0;
}

int wvm_admission_evidence_owner_refresh_input(
    void *context, enum wvm_admission_input_phase phase,
    const struct wvm_vm_request *request,
    const struct wvm_coordinator_transaction *transaction,
    struct wvm_admission_orchestrator_input *input, char *error,
    size_t error_len)
{
    struct wvm_admission_evidence_owner *owner = context;

    if (!request || !transaction || !input ||
        (phase != WVM_ADMISSION_INPUT_PREPARE &&
         phase != WVM_ADMISSION_INPUT_ACTIVATION)) {
        set_error(error, error_len, "admission evidence refresh input is invalid");
        return -1;
    }
    if (!owner) {
        set_error(error, error_len, "admission evidence owner is missing");
        return -1;
    }
    /* The owner publication is immutable for the coordinator operation. */
    if (wvm_admission_evidence_owner_capture(owner, &owner->evidence_view,
                                             error, error_len) != 0) {
        return -1;
    }
    input->membership_evidence = &owner->evidence_view;
    return 0;
}
