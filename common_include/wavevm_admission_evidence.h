#ifndef WAVEVM_ADMISSION_EVIDENCE_H
#define WAVEVM_ADMISSION_EVIDENCE_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_coordinator.h"

struct wvm_admission_orchestrator_input;
enum wvm_admission_input_phase;

/*
 * The evidence owner is the boundary between capability/reservation providers
 * and admission. It stores one validated publication in caller-supplied
 * bounded storage. Publication is a control-plane operation: callers must not
 * replace the publication while a coordinator operation is using its captured
 * view.
 */
struct wvm_admission_evidence_owner {
    struct wvm_capability_record *capability_records;
    size_t capability_record_capacity;
    size_t capability_record_count;
    struct wvm_resource_reservation *resource_reservations;
    size_t resource_reservation_capacity;
    size_t resource_reservation_count;
    uint64_t inventory_revision;
    uint64_t capability_profile_generation;
    struct wvm_coordinator_membership_evidence evidence_view;
    int initialized;
    int published;
};

int wvm_admission_evidence_owner_init(
    struct wvm_admission_evidence_owner *owner,
    struct wvm_capability_record *capability_records,
    size_t capability_record_capacity,
    struct wvm_resource_reservation *resource_reservations,
    size_t resource_reservation_capacity, char *error, size_t error_len);

/*
 * Publish a complete immutable provider view. The owner copies the top-level
 * records into its bounded storage; nested capability and lease lists remain
 * caller-owned and must stay immutable for the owner's lifetime.
 */
int wvm_admission_evidence_owner_publish(
    struct wvm_admission_evidence_owner *owner,
    const struct wvm_capability_record *capability_records,
    size_t capability_record_count,
    const struct wvm_resource_reservation *resource_reservations,
    size_t resource_reservation_count, uint64_t inventory_revision,
    uint64_t capability_profile_generation, char *error, size_t error_len);

int wvm_admission_evidence_owner_validate(
    const struct wvm_admission_evidence_owner *owner, char *error,
    size_t error_len);

/* Bind the validated publication to one coordinator evidence view. */
int wvm_admission_evidence_owner_capture(
    const struct wvm_admission_evidence_owner *owner,
    struct wvm_coordinator_membership_evidence *evidence, char *error,
    size_t error_len);

/* Direct adapter for the orchestrator's refresh_input provider boundary. */
int wvm_admission_evidence_owner_refresh_input(
    void *context, enum wvm_admission_input_phase phase,
    const struct wvm_vm_request *request,
    const struct wvm_coordinator_transaction *transaction,
    struct wvm_admission_orchestrator_input *input, char *error,
    size_t error_len);

#endif /* WAVEVM_ADMISSION_EVIDENCE_H */
