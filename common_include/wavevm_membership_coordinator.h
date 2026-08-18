#ifndef WAVEVM_MEMBERSHIP_COORDINATOR_H
#define WAVEVM_MEMBERSHIP_COORDINATOR_H

/*
 * Control-plane join orchestration. The durable membership controller remains
 * the authority; this layer only composes its idempotent operations and hands
 * real route-prepare transport to the caller.
 */

#include <stddef.h>

#include "wavevm_membership_controller.h"

typedef int (*wvm_membership_route_prepare_fn)(
    void *context, const struct wvm_route_transaction_record *transaction,
    const struct wvm_required_ack_entry *ack_entry, char *error,
    size_t error_len);

struct wvm_membership_join_request {
    enum wvm_membership_member_kind member_kind;
    const struct wvm_member_key *authenticated_actor;
    const struct wvm_node_record *node;
    const struct wvm_gateway_record *gateway;
    const struct wvm_route_transaction_record *route_transaction;
    wvm_membership_route_prepare_fn route_prepare;
    void *route_prepare_context;
};

/*
 * Register/validate/prepare one member, publish one complete route snapshot,
 * collect the required remote prepare acknowledgements, commit the route, and
 * activate the joining member. A failure before route commit aborts the route
 * transaction; the member remains non-active and therefore non-schedulable.
 * The route callback must be idempotent for the supplied operation ID.
 */
int wvm_membership_coordinator_join(
    struct wvm_membership_controller *controller,
    const struct wvm_membership_join_request *request, char *error,
    size_t error_len);

#endif /* WAVEVM_MEMBERSHIP_COORDINATOR_H */
