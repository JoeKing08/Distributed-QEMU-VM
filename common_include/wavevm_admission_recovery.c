#include "wavevm_admission_orchestrator.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "wavevm_membership.h"

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

static int recovery_callbacks_valid(
    const struct wvm_admission_orchestrator_callbacks *callbacks,
    char *error, size_t error_len)
{
    if (!callbacks || !callbacks->route_commit || !callbacks->route_abort ||
        !callbacks->reservation_commit || !callbacks->reservation_abort ||
        !callbacks->participant_commit || !callbacks->participant_abort ||
        !callbacks->participant_ready) {
        set_error(error, error_len,
                  "admission recovery callbacks are incomplete");
        return -1;
    }
    return 0;
}

static int recover_reservations(
    const struct wvm_admission_recovery_input *input,
    wvm_admission_reservation_stage_fn callback, char *error, size_t error_len)
{
    size_t i;

    for (i = 0; i < input->prepared_vm->reservation_count; i++) {
        if (callback(input->callback_context,
                     &input->prepared_vm->reservations[i], error,
                     error_len) != 0) {
            return -1;
        }
    }
    return 0;
}

static int recover_participants(
    const struct wvm_admission_recovery_input *input,
    wvm_admission_participant_stage_fn callback, char *error, size_t error_len)
{
    size_t i;

    for (i = 0; i < input->prepared_vm->node_runtime_manifest_count; i++) {
        if (callback(input->callback_context,
                     &input->prepared_vm->node_runtime_manifests[i], error,
                     error_len) != 0) {
            return -1;
        }
    }
    return 0;
}

static int durable_route_state(
    const struct wvm_admission_recovery_input *input, uint16_t state,
    char *error, size_t error_len)
{
    input->route_transaction->state = state;
    return wvm_control_plane_record_route_transaction(
        input->control_plane, input->route_transaction, error, error_len);
}

static int recovery_input_valid(
    const struct wvm_admission_recovery_input *input, char *error,
    size_t error_len)
{
    if (!input || !input->control_plane || !input->transaction ||
        !input->prepared_vm || !input->activation ||
        !input->route_transaction ||
        recovery_callbacks_valid(input->callbacks, error, error_len) != 0) {
        set_error(error, error_len, "admission recovery input is invalid");
        return -1;
    }
    return 0;
}

static int recover_activation(
    const struct wvm_admission_recovery_input *input, char *error,
    size_t error_len)
{
    size_t i;

    if (input->activation->decision != WVM_ACTIVATION_ACTIVATE ||
        !input->activation->has_activation_fence ||
        wvm_coordinator_commit_local(input->transaction, input->prepared_vm,
                                     input->activation, error, error_len) != 0 ||
        recover_reservations(input, input->callbacks->reservation_commit, error,
                             error_len) != 0 ||
        recover_participants(input, input->callbacks->participant_commit, error,
                             error_len) != 0) {
        return -1;
    }
    for (i = 0; i < input->prepared_vm->node_runtime_manifest_count; i++) {
        if (wvm_control_plane_record_runtime_manifest(
                input->control_plane, input->transaction,
                &input->prepared_vm->node_runtime_manifests[i], error,
                error_len) != 0) {
            return -1;
        }
    }
    if (input->callbacks->route_commit(input->callback_context,
                                       input->route_transaction, error,
                                       error_len) != 0 ||
        durable_route_state(input, WVM_ROUTE_TRANSACTION_ACTIVATED, error,
                            error_len) != 0 ||
        wvm_control_plane_transition(
            input->control_plane, input->transaction,
            WVM_LIFECYCLE_ACTIVATION_DECIDED, WVM_LIFECYCLE_COMMITTED, error,
            error_len) != 0 ||
        recover_participants(input, input->callbacks->participant_ready, error,
                             error_len) != 0 ||
        wvm_control_plane_start_if_ready(
            input->control_plane, input->transaction,
            input->prepared_vm->node_runtime_manifests,
            input->prepared_vm->node_runtime_manifest_count, error,
            error_len) != 0) {
        return -1;
    }
    return 0;
}

static int recover_abort(
    const struct wvm_admission_recovery_input *input, char *error,
    size_t error_len)
{
    if (input->activation->decision != WVM_ACTIVATION_ABORT ||
        input->activation->has_activation_fence ||
        recover_participants(input, input->callbacks->participant_abort, error,
                             error_len) != 0 ||
        recover_reservations(input, input->callbacks->reservation_abort, error,
                             error_len) != 0 ||
        wvm_coordinator_abort_local(input->transaction, input->prepared_vm,
                                    input->activation, error, error_len) != 0 ||
        input->callbacks->route_abort(input->callback_context,
                                      input->route_transaction, error,
                                      error_len) != 0 ||
        durable_route_state(input, WVM_ROUTE_TRANSACTION_ABORTED, error,
                            error_len) != 0 ||
        wvm_control_plane_transition(
            input->control_plane, input->transaction, WVM_LIFECYCLE_ABORTING,
            WVM_LIFECYCLE_ABORTED, error, error_len) != 0) {
        return -1;
    }
    return 0;
}

int wvm_admission_orchestrator_recover(
    const struct wvm_admission_recovery_input *input, char *error,
    size_t error_len)
{
    const struct wvm_control_plane_entry *entry;

    if (recovery_input_valid(input, error, error_len) != 0) {
        return -1;
    }
    entry = wvm_control_plane_find_request(
        input->control_plane, input->transaction->request_id);
    if (!entry || entry->transaction.vm_id != input->transaction->vm_id ||
        entry->transaction.vm_incarnation != input->transaction->vm_incarnation ||
        memcmp(entry->transaction.admission_tx_id,
               input->transaction->admission_tx_id,
               sizeof(entry->transaction.admission_tx_id)) != 0) {
        set_error(error, error_len,
                  "admission recovery transaction identity mismatch");
        return -1;
    }
    if (entry->transaction.state == WVM_LIFECYCLE_RUNNING) {
        return 0;
    }
    if (entry->transaction.state == WVM_LIFECYCLE_COMMITTED) {
        if (recover_participants(input, input->callbacks->participant_ready,
                                 error, error_len) != 0) {
            return -1;
        }
        return wvm_control_plane_start_if_ready(
            input->control_plane, input->transaction,
            input->prepared_vm->node_runtime_manifests,
            input->prepared_vm->node_runtime_manifest_count, error,
            error_len);
    }
    if (entry->transaction.state == WVM_LIFECYCLE_ACTIVATION_DECIDED) {
        return recover_activation(input, error, error_len);
    }
    if (entry->transaction.state == WVM_LIFECYCLE_ABORTING) {
        return recover_abort(input, error, error_len);
    }
    set_error(error, error_len,
              "admission recovery cannot resume lifecycle state %d",
              entry->transaction.state);
    return -1;
}
