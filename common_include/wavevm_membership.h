#ifndef WAVEVM_MEMBERSHIP_H
#define WAVEVM_MEMBERSHIP_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_manifest.h"

enum wvm_membership_health_state {
    WVM_MEMBERSHIP_HEALTHY = 1,
    WVM_MEMBERSHIP_SUSPECT = 2,
    WVM_MEMBERSHIP_UNREACHABLE = 3,
    WVM_MEMBERSHIP_RECOVERING = 4,
};

enum wvm_membership_member_kind {
    WVM_MEMBERSHIP_COMPUTE = 1,
    WVM_MEMBERSHIP_GATEWAY = 2,
};

enum wvm_route_topology_kind {
    WVM_ROUTE_TOPOLOGY_FLAT = 1,
    WVM_ROUTE_TOPOLOGY_FRACTAL = 2,
};

enum wvm_route_transaction_state {
    WVM_ROUTE_TRANSACTION_PREPARING = 1,
    WVM_ROUTE_TRANSACTION_ACTIVATED = 2,
    WVM_ROUTE_TRANSACTION_RETIRING = 3,
    WVM_ROUTE_TRANSACTION_RETIRED = 4,
    WVM_ROUTE_TRANSACTION_ABORTED = 5,
};

struct wvm_membership_member {
    enum wvm_membership_member_kind kind;
    struct wvm_member_key member_key;
    uint32_t hosting_physical_node_id;
    uint64_t failure_domain_id;
    struct wvm_capability_ref capability;
    enum wvm_manifest_member_state desired_state;
    enum wvm_membership_health_state health_state;
    uint64_t membership_revision;
    uint64_t topology_revision;
    uint64_t active_dependency_count;
};

struct wvm_membership_dependency {
    struct wvm_member_key member_key;
    uint32_t vm_id;
    uint64_t vm_incarnation;
    uint64_t manifest_generation;
    uint16_t dependency_kind;
};

struct wvm_membership_registry {
    struct wvm_membership_member *members;
    size_t member_count;
    size_t member_capacity;
    struct wvm_membership_dependency *dependencies;
    size_t dependency_count;
    size_t dependency_capacity;
    uint64_t membership_revision;
    uint64_t topology_revision;
    uint64_t admission_eligibility_revision;
};

struct wvm_route_ack_entry {
    struct wvm_member_key member_key;
    struct wvm_route_snapshot_key expected_snapshot_key;
    int prepared;
    int activated;
};

struct wvm_route_ack_set {
    struct wvm_route_ack_entry *entries;
    size_t count;
    size_t capacity;
    uint8_t entries_digest[WVM_SHA256_DIGEST_BYTES];
};

struct wvm_route_rule {
    uint16_t destination_kind;
    uint64_t destination_scope;
    uint32_t destination_vnode_or_endpoint;
    uint16_t next_hop_kind;
    struct wvm_member_key next_hop_member;
    uint16_t hop_limit;
};

struct wvm_route_snapshot {
    struct wvm_route_snapshot_key key;
    uint64_t membership_revision;
    enum wvm_route_topology_kind topology_kind;
    struct wvm_route_rule *rules;
    size_t rule_count;
    size_t rule_capacity;
    const struct wvm_route_ack_set *required_ack_set;
    int has_predecessor;
    struct wvm_route_snapshot_key predecessor_key;
    uint64_t operation_retention_horizon_ms;
    uint16_t retirement_policy;
};

struct wvm_route_transaction {
    uint8_t operation_id[WVM_IDENTITY_ID_BYTES];
    const struct wvm_route_snapshot *successor;
    int has_predecessor;
    struct wvm_route_snapshot_key predecessor_key;
    struct wvm_route_ack_set required_ack_set;
    enum wvm_route_transaction_state state;
    uint64_t operation_retention_horizon_ms;
};

void wvm_membership_registry_init(struct wvm_membership_registry *registry,
                                  struct wvm_membership_member *members,
                                  size_t member_capacity);

/*
 * Dependency storage is explicit because member drain/removal must enumerate
 * admitted VM consumers rather than relying on an externally mutable count.
 */
void wvm_membership_registry_set_dependencies(
    struct wvm_membership_registry *registry,
    struct wvm_membership_dependency *dependencies, size_t dependency_capacity);

int wvm_membership_member_validate(const struct wvm_membership_member *member,
                                   char *error, size_t error_len);

int wvm_membership_register(struct wvm_membership_registry *registry,
                            const struct wvm_membership_member *member,
                            char *error, size_t error_len);
int wvm_membership_begin_validation(struct wvm_membership_registry *registry,
                                    const struct wvm_member_key *member_key,
                                    char *error, size_t error_len);
int wvm_membership_prepare(struct wvm_membership_registry *registry,
                           const struct wvm_member_key *member_key,
                           char *error, size_t error_len);

/*
 * Direct activation is intentionally rejected.  A member becomes ACTIVE only
 * through a committed route transaction that includes it as a prepared route
 * participant, preventing an endpoint or heartbeat from becoming authority.
 */
int wvm_membership_activate(struct wvm_membership_registry *registry,
                            const struct wvm_member_key *member_key,
                            char *error, size_t error_len);
int wvm_membership_activate_with_route(
    struct wvm_membership_registry *registry,
    const struct wvm_member_key *member_key,
    const struct wvm_route_transaction *transaction, char *error,
    size_t error_len);
int wvm_membership_cordon(struct wvm_membership_registry *registry,
                          const struct wvm_member_key *member_key,
                          char *error, size_t error_len);
int wvm_membership_begin_drain(struct wvm_membership_registry *registry,
                               const struct wvm_member_key *member_key,
                               char *error, size_t error_len);
int wvm_membership_remove(struct wvm_membership_registry *registry,
                          const struct wvm_member_key *member_key,
                          char *error, size_t error_len);
int wvm_membership_mark_health(struct wvm_membership_registry *registry,
                               const struct wvm_member_key *member_key,
                               enum wvm_membership_health_state health_state,
                               char *error, size_t error_len);
int wvm_membership_dependency_acquire(
    struct wvm_membership_registry *registry,
    const struct wvm_membership_dependency *dependency, char *error,
    size_t error_len);
int wvm_membership_dependency_release(
    struct wvm_membership_registry *registry,
    const struct wvm_membership_dependency *dependency, char *error,
    size_t error_len);

const struct wvm_membership_member *
wvm_membership_find(const struct wvm_membership_registry *registry,
                    const struct wvm_member_key *member_key);

int wvm_membership_is_admission_eligible(
    const struct wvm_membership_member *member);

int wvm_route_snapshot_validate(const struct wvm_route_snapshot *snapshot,
                                const struct wvm_membership_registry *registry,
                                char *error, size_t error_len);

int wvm_route_transaction_begin(
    struct wvm_route_transaction *transaction,
    const uint8_t operation_id[WVM_IDENTITY_ID_BYTES],
    const struct wvm_route_snapshot *successor,
    const struct wvm_route_snapshot_key *predecessor_key,
    struct wvm_route_ack_entry *required_ack_entries,
    size_t required_ack_capacity, size_t required_ack_count,
    const uint8_t required_ack_set_digest[WVM_SHA256_DIGEST_BYTES],
    char *error, size_t error_len);

int wvm_route_transaction_ack_prepare(
    struct wvm_route_transaction *transaction,
    const struct wvm_member_key *member_key,
    const struct wvm_route_snapshot_key *snapshot_key,
    char *error, size_t error_len);

int wvm_route_transaction_commit(
    struct wvm_route_transaction *transaction,
    const struct wvm_membership_registry *registry, char *error,
    size_t error_len);

int wvm_route_transaction_begin_retire(
    struct wvm_route_transaction *transaction, uint64_t active_operation_refs,
    int retention_horizon_complete, char *error, size_t error_len);

int wvm_route_transaction_retire(struct wvm_route_transaction *transaction,
                                 char *error, size_t error_len);

int wvm_route_transaction_abort(struct wvm_route_transaction *transaction,
                                char *error, size_t error_len);

#endif /* WAVEVM_MEMBERSHIP_H */
