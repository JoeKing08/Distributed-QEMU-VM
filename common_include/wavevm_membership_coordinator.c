#include "wavevm_coordinator.h"
#include "wavevm_membership_coordinator.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "wavevm_membership_controller.h"

static void set_error(char *error, size_t error_len, const char *fmt, ...);

static int operation_id_is_zero(const uint8_t operation_id[WVM_IDENTITY_ID_BYTES])
{
    size_t i;

    for (i = 0; i < WVM_IDENTITY_ID_BYTES; i++) {
        if (operation_id[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static int member_key_equal_local(const struct wvm_member_key *left,
                                  const struct wvm_member_key *right)
{
    return left && right && left->role_type == right->role_type &&
           left->role_id == right->role_id &&
           left->instance_id == right->instance_id;
}

static int join_member_key(const struct wvm_membership_join_request *request,
                           struct wvm_member_key *member_key, char *error,
                           size_t error_len)
{
    if (!request || !member_key ||
        (request->member_kind != WVM_MEMBERSHIP_COMPUTE &&
         request->member_kind != WVM_MEMBERSHIP_GATEWAY)) {
        set_error(error, error_len, "membership join kind is invalid");
        return -1;
    }
    if (request->member_kind == WVM_MEMBERSHIP_COMPUTE) {
        if (!request->node) {
            set_error(error, error_len, "compute join record is missing");
            return -1;
        }
        memset(member_key, 0, sizeof(*member_key));
        member_key->role_type = WVM_MANIFEST_ROLE_NODE_RUNTIME;
        member_key->role_id = request->node->physical_node_id;
        member_key->instance_id = request->node->node_instance_id;
    } else {
        if (!request->gateway) {
            set_error(error, error_len, "gateway join record is missing");
            return -1;
        }
        memset(member_key, 0, sizeof(*member_key));
        member_key->role_type = WVM_MANIFEST_ROLE_GATEWAY;
        member_key->role_id = request->gateway->gateway_id;
        member_key->instance_id = request->gateway->gateway_instance_id;
    }
    return wvm_member_key_validate(member_key, error, error_len);
}

static int count_join_member_ack(
    const struct wvm_route_transaction_record *transaction,
    const struct wvm_member_key *member_key, size_t *index_out, char *error,
    size_t error_len)
{
    size_t i;
    size_t found = 0;
    size_t index = 0;

    if (!transaction || !member_key || !index_out) {
        set_error(error, error_len, "membership join ACK input is invalid");
        return -1;
    }
    for (i = 0; i < transaction->required_ack_set.entries.count; i++) {
        const struct wvm_required_ack_entry *entry =
            &transaction->required_ack_set.entries.entries[i];

        if (entry->member_key.role_type == member_key->role_type &&
            entry->member_key.role_id == member_key->role_id &&
            entry->member_key.instance_id == member_key->instance_id) {
            found++;
            index = i;
        }
    }
    if (found != 1) {
        set_error(error, error_len,
                  "join member must appear exactly once in RequiredAckSet");
        return -1;
    }
    *index_out = index;
    return 0;
}

static int abort_prepared_route(struct wvm_membership_controller *controller,
                                const uint8_t operation_id[WVM_IDENTITY_ID_BYTES],
                                char *error, size_t error_len)
{
    uint16_t state;

    if (wvm_membership_controller_route_state(
            controller, operation_id, &state, error, error_len) != 0) {
        return -1;
    }
    if (state == WVM_ROUTE_TRANSACTION_PREPARING &&
        wvm_membership_controller_route_abort(controller, operation_id, error,
                                              error_len) != 0) {
        return -1;
    }
    return 0;
}

int wvm_membership_coordinator_join(
    struct wvm_membership_controller *controller,
    const struct wvm_membership_join_request *request, char *error,
    size_t error_len)
{
    struct wvm_member_key member_key;
    uint16_t route_state;
    size_t joining_ack_index;
    size_t i;
    int result;

    if (!controller || !request || !request->authenticated_actor ||
        !request->route_transaction || !request->route_prepare ||
        operation_id_is_zero(request->route_transaction->operation_id)) {
        set_error(error, error_len,
                  "membership join orchestration input is invalid");
        return -1;
    }
    if (join_member_key(request, &member_key, error, error_len) != 0 ||
        !member_key_equal_local(request->authenticated_actor, &member_key) ||
        count_join_member_ack(request->route_transaction, &member_key,
                              &joining_ack_index, error, error_len) != 0) {
        set_error(error, error_len,
                  "membership join actor or route participant is invalid");
        return -1;
    }
    if (request->member_kind == WVM_MEMBERSHIP_COMPUTE) {
        result = wvm_membership_controller_register_node(
            controller, request->authenticated_actor, request->node, error,
            error_len);
    } else {
        result = wvm_membership_controller_register_gateway(
            controller, request->authenticated_actor, request->gateway, error,
            error_len);
    }
    if (result != 0 ||
        wvm_membership_controller_report_self_health(
            controller, request->authenticated_actor, WVM_MEMBERSHIP_HEALTHY,
            error, error_len) != 0 ||
        wvm_membership_controller_prepare_member_for_route(
            controller, &member_key, request->route_transaction->operation_id,
            error, error_len) != 0) {
        return -1;
    }

    if (wvm_membership_controller_route_begin(
            controller, request->route_transaction, error, error_len) != 0 ||
        wvm_membership_controller_route_state(
            controller, request->route_transaction->operation_id, &route_state,
            error, error_len) != 0) {
        return -1;
    }
    if (route_state != WVM_ROUTE_TRANSACTION_PREPARING &&
        route_state != WVM_ROUTE_TRANSACTION_ACTIVATED) {
        set_error(error, error_len,
                  "join route operation is not replayable or preparing");
        return -1;
    }

    if (route_state == WVM_ROUTE_TRANSACTION_PREPARING) {
        for (i = 0; i < request->route_transaction->required_ack_set.entries.count;
             i++) {
            const struct wvm_required_ack_entry *ack =
                &request->route_transaction->required_ack_set.entries.entries[i];

            if (request->route_prepare(request->route_prepare_context,
                                       request->route_transaction, ack, error,
                                       error_len) != 0 ||
                wvm_membership_controller_route_ack_prepare(
                    controller, request->route_transaction->operation_id,
                    &ack->member_key, error, error_len) != 0) {
                if (abort_prepared_route(
                        controller, request->route_transaction->operation_id,
                        error, error_len) != 0) {
                    return -1;
                }
                return -1;
            }
        }
        if (wvm_membership_controller_route_commit(
                controller, request->route_transaction->operation_id, error,
                error_len) != 0) {
            if (abort_prepared_route(
                    controller, request->route_transaction->operation_id, error,
                    error_len) != 0) {
                return -1;
            }
            return -1;
        }
    }
    if (wvm_membership_controller_activate_member(
            controller, &member_key, request->route_transaction->operation_id,
            error, error_len) != 0) {
        return -1;
    }
    (void)joining_ack_index;
    return 0;
}

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

int wvm_coordinator_capture_current_membership_records(
    const struct wvm_membership_controller *membership_controller,
    struct wvm_membership_controller_capture *membership_capture,
    const struct wvm_coordinator_membership_evidence *evidence,
    struct wvm_cluster_record_set *records_out, char *error, size_t error_len)
{
    if (!membership_controller || !membership_capture || !evidence ||
        !records_out) {
        set_error(error, error_len,
                  "coordinator membership capture input is invalid");
        return -1;
    }
    return wvm_membership_controller_capture_current_cluster_records(
        membership_controller, membership_capture, evidence->capability_records,
        evidence->capability_record_count, evidence->resource_reservations,
        evidence->resource_reservation_count, evidence->inventory_revision,
        evidence->capability_profile_generation, records_out, error, error_len);
}

int wvm_coordinator_prepare_current_membership(
    const struct wvm_membership_controller *membership_controller,
    struct wvm_membership_controller_capture *membership_capture,
    const struct wvm_coordinator_membership_evidence *evidence,
    const struct wvm_vm_request *request,
    const struct wvm_coordinator_transaction *transaction,
    const struct wvm_coordinator_prepared_route *prepared_route,
    const struct wvm_coordinator_prepare_options *options,
    struct wvm_coordinator_prepared_vm *prepared_vm, char *error,
    size_t error_len)
{
    struct wvm_cluster_record_set records;

    if (wvm_coordinator_capture_current_membership_records(
            membership_controller, membership_capture, evidence, &records,
            error, error_len) != 0) {
        return -1;
    }
    return wvm_coordinator_prepare(request, transaction, &records,
                                   prepared_route, options, prepared_vm, error,
                                   error_len);
}

int wvm_coordinator_decide_activation_current_membership(
    const struct wvm_membership_controller *membership_controller,
    struct wvm_membership_controller_capture *membership_capture,
    const struct wvm_coordinator_membership_evidence *evidence,
    const struct wvm_vm_request *request,
    const struct wvm_coordinator_transaction *transaction,
    const struct wvm_coordinator_prepared_route *prepared_route,
    const struct wvm_coordinator_id_provider *id_provider,
    const struct wvm_coordinator_activation_options *options,
    struct wvm_coordinator_prepared_vm *prepared_vm,
    struct wvm_activation_record *activation, char *error, size_t error_len)
{
    struct wvm_cluster_record_set records;

    if (wvm_coordinator_capture_current_membership_records(
            membership_controller, membership_capture, evidence, &records,
            error, error_len) != 0) {
        return -1;
    }
    return wvm_coordinator_decide_activation(
        request, transaction, &records, prepared_route, id_provider, options,
        prepared_vm, activation, error, error_len);
}
