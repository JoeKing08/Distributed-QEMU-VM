#ifndef WAVEVM_ADMISSION_ORCHESTRATOR_H
#define WAVEVM_ADMISSION_ORCHESTRATOR_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_control_plane.h"

/*
 * These callbacks are transport boundaries.  Each implementation must be
 * idempotent for the identity and manifest passed to it.  A failed prepare or
 * abort callback may have made partial progress, so abort callbacks must also
 * tolerate the item that failed to prepare.
 */
typedef int (*wvm_admission_route_stage_fn)(
    void *context, const struct wvm_route_transaction_record *transaction,
    char *error, size_t error_len);

typedef int (*wvm_admission_route_plan_fn)(
    void *context, const struct wvm_coordinator_transaction *transaction,
    struct wvm_coordinator_prepared_route *prepared_route,
    struct wvm_route_transaction_record *route_transaction, char *error,
    size_t error_len);

typedef int (*wvm_admission_reservation_stage_fn)(
    void *context, const struct wvm_resource_reservation *reservation,
    char *error, size_t error_len);

typedef int (*wvm_admission_participant_stage_fn)(
    void *context, const struct wvm_node_runtime_manifest *runtime_manifest,
    char *error, size_t error_len);

struct wvm_admission_orchestrator_callbacks {
    wvm_admission_route_plan_fn route_plan;
    wvm_admission_route_stage_fn route_prepare;
    wvm_admission_route_stage_fn route_commit;
    wvm_admission_route_stage_fn route_abort;
    wvm_admission_reservation_stage_fn reservation_prepare;
    wvm_admission_reservation_stage_fn reservation_commit;
    wvm_admission_reservation_stage_fn reservation_abort;
    wvm_admission_participant_stage_fn participant_prepare;
    wvm_admission_participant_stage_fn participant_commit;
    wvm_admission_participant_stage_fn participant_abort;
    wvm_admission_participant_stage_fn participant_ready;
};

struct wvm_admission_orchestrator_input {
    struct wvm_control_plane *control_plane;
    struct wvm_vm_namespace_allocator *namespace_allocator;
    const struct wvm_coordinator_id_provider *id_provider;
    const struct wvm_vm_request *request;
    const struct wvm_cluster_record_set *records;
    struct wvm_coordinator_prepared_route *prepared_route;
    const struct wvm_coordinator_prepare_options *prepare_options;
    struct wvm_coordinator_prepared_vm *prepared_vm;
    const struct wvm_coordinator_activation_options *activation_options;
    struct wvm_activation_record *activation;
    struct wvm_route_transaction_record *route_transaction;
    const struct wvm_admission_orchestrator_callbacks *callbacks;
    void *callback_context;
    struct wvm_coordinator_transaction *transaction_out;
    enum wvm_control_plane_submit_result *submit_result_out;
};

/*
 * Execute one create transaction through durable prepare/activate/commit.
 * A replay returns success without invoking transport callbacks.  If any
 * pre-activation stage fails, the function records ABORTING and cleans up
 * only prepared resources; it reaches ABORTED only when cleanup succeeds.
 * Any failure after ACTIVATE is durable leaves ACTIVATION_DECIDED for the
 * recovery/reconciliation path and never issues a pre-activation abort.
 */
int wvm_admission_orchestrator_run(
    const struct wvm_admission_orchestrator_input *input, char *error,
    size_t error_len);

struct wvm_admission_recovery_input {
    struct wvm_control_plane *control_plane;
    const struct wvm_coordinator_transaction *transaction;
    struct wvm_coordinator_prepared_vm *prepared_vm;
    const struct wvm_activation_record *activation;
    struct wvm_route_transaction_record *route_transaction;
    const struct wvm_admission_orchestrator_callbacks *callbacks;
    void *callback_context;
};

/*
 * Resume a durable transaction after process loss. The caller supplies
 * identity-validated in-memory projections reconstructed from the durable
 * candidate/runtime records; this function never invents a replacement
 * manifest or route. ACTIVATION_DECIDED is always resumed forward, while
 * ABORTING is cleaned up toward ABORTED.
 */
int wvm_admission_orchestrator_recover(
    const struct wvm_admission_recovery_input *input, char *error,
    size_t error_len);

#endif /* WAVEVM_ADMISSION_ORCHESTRATOR_H */
