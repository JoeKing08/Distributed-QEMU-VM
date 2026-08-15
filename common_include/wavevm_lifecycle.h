#ifndef WAVEVM_LIFECYCLE_H
#define WAVEVM_LIFECYCLE_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_manifest.h"

#define WVM_RECORD_RESOURCE_RESERVATION 0x1013U
#define WVM_RECORD_NODE_RUNTIME_MANIFEST 0x101eU
#define WVM_RECORD_ACTIVATION_RECORD 0x101fU
#define WVM_RECORD_ADMISSION_TRANSACTION 0x1027U

enum wvm_reservation_state {
    WVM_RESERVATION_PREPARED = 1,
    WVM_RESERVATION_COMMITTED = 2,
    WVM_RESERVATION_RELEASING = 3,
    WVM_RESERVATION_RELEASED = 4,
};

enum wvm_activation_decision {
    WVM_ACTIVATION_ACTIVATE = 1,
    WVM_ACTIVATION_ABORT = 2,
};

enum wvm_lifecycle_state {
    WVM_LIFECYCLE_ABSENT = 0,
    WVM_LIFECYCLE_REQUESTED = 1,
    WVM_LIFECYCLE_VALIDATING = 2,
    WVM_LIFECYCLE_IDENTITY_ALLOCATED = 3,
    WVM_LIFECYCLE_PLANNED = 4,
    WVM_LIFECYCLE_ROUTE_SCOPE_PREPARED = 5,
    WVM_LIFECYCLE_RESERVATIONS_PREPARED = 6,
    WVM_LIFECYCLE_PARTICIPANTS_PREPARED = 7,
    WVM_LIFECYCLE_ACTIVATION_DECIDED = 8,
    WVM_LIFECYCLE_COMMITTED = 9,
    WVM_LIFECYCLE_STARTING = 10,
    WVM_LIFECYCLE_RUNNING = 11,
    WVM_LIFECYCLE_PAUSING = 12,
    WVM_LIFECYCLE_PAUSED = 13,
    WVM_LIFECYCLE_DEGRADED = 14,
    WVM_LIFECYCLE_STOPPING = 15,
    WVM_LIFECYCLE_RETIRING = 16,
    WVM_LIFECYCLE_STOPPED = 17,
    WVM_LIFECYCLE_ABORTING = 18,
    WVM_LIFECYCLE_ABORTED = 19,
    WVM_LIFECYCLE_FAILED = 20,
};

/*
 * Runtime startup dependencies name external participants whose admitted
 * identity and required membership state must be observed before this local
 * projection admits guest traffic.
 */
enum wvm_startup_dependency_kind {
    WVM_STARTUP_DEPENDENCY_REQUIRED_MEMBER = 1,
};

struct wvm_startup_dependency {
    enum wvm_startup_dependency_kind dependency_kind;
    struct wvm_member_key member_key;
    enum wvm_manifest_member_state required_state;
    uint8_t dependency_digest[WVM_SHA256_DIGEST_BYTES];
};

struct wvm_startup_dependency_list {
    struct wvm_startup_dependency *entries;
    size_t count;
    size_t capacity;
};

struct wvm_resource_reservation {
    uint8_t reservation_id[WVM_IDENTITY_ID_BYTES];
    uint8_t plan_digest[WVM_SHA256_DIGEST_BYTES];
    uint8_t candidate_manifest_digest[WVM_SHA256_DIGEST_BYTES];
    uint8_t admission_tx_id[WVM_IDENTITY_ID_BYTES];
    uint8_t eligibility_fence_digest[WVM_SHA256_DIGEST_BYTES];
    uint32_t vm_id;
    uint64_t vm_incarnation;
    uint32_t physical_node_id;
    uint64_t node_instance_id;
    uint64_t inventory_revision;
    uint32_t guest_vcpu_slots;
    uint64_t guest_memory_bytes;
    uint32_t overhead_vcpu_slots;
    uint64_t overhead_memory_bytes;
    struct wvm_exclusive_lease_list exclusive_leases;
    enum wvm_reservation_state state;
    int has_prepared_expiry;
    uint64_t prepared_expiry_unix_time_ms;
    int has_activation_fence;
    uint8_t activation_fence[WVM_IDENTITY_ID_BYTES];
};

struct wvm_activation_record {
    uint8_t admission_tx_id[WVM_IDENTITY_ID_BYTES];
    uint8_t candidate_manifest_digest[WVM_SHA256_DIGEST_BYTES];
    int has_activation_fence;
    uint8_t activation_fence[WVM_IDENTITY_ID_BYTES];
    uint64_t coordinator_instance_id;
    uint8_t required_participant_set_digest[WVM_SHA256_DIGEST_BYTES];
    struct wvm_route_snapshot_key *required_route_snapshot_keys;
    size_t required_route_snapshot_count;
    size_t required_route_snapshot_capacity;
    enum wvm_activation_decision decision;
    uint64_t durable_decision_sequence;
    uint64_t decided_at;
};

struct wvm_node_runtime_manifest {
    uint8_t candidate_manifest_digest[WVM_SHA256_DIGEST_BYTES];
    uint32_t vm_id;
    uint64_t vm_incarnation;
    uint64_t manifest_generation;
    uint8_t admission_tx_id[WVM_IDENTITY_ID_BYTES];
    uint8_t eligibility_fence_digest[WVM_SHA256_DIGEST_BYTES];
    int has_activation_fence;
    uint8_t activation_fence[WVM_IDENTITY_ID_BYTES];
    uint32_t physical_node_id;
    uint64_t expected_node_instance_id;
    uint64_t local_role_bits;
    struct wvm_vcpu_assignment_list local_vcpu_assignments;
    struct wvm_memory_chunk_assignment_list local_memory_assignments;
    struct wvm_storage_assignment_list local_storage_assignments;
    struct wvm_route_snapshot_key required_route_snapshot_key;
    struct wvm_local_name_namespace local_names;
    struct wvm_execution_fault_profile negotiated_profile;
    uint8_t reservation_id[WVM_IDENTITY_ID_BYTES];
    struct wvm_startup_dependency_list startup_dependencies;
};

struct wvm_lifecycle_transaction {
    uint32_t vm_id;
    uint64_t vm_incarnation;
    uint8_t admission_tx_id[WVM_IDENTITY_ID_BYTES];
    uint8_t candidate_manifest_digest[WVM_SHA256_DIGEST_BYTES];
    enum wvm_lifecycle_state state;
};

/*
 * Durable coordinator index for one create request. Candidate manifests and
 * activation decisions remain separate canonical records; this binds their
 * immutable digests to request-id idempotency and lifecycle state.
 */
struct wvm_admission_transaction_record {
    uint8_t request_id[WVM_IDENTITY_ID_BYTES];
    uint8_t request_digest[WVM_SHA256_DIGEST_BYTES];
    uint32_t vm_id;
    uint64_t vm_incarnation;
    uint64_t manifest_generation;
    uint8_t admission_tx_id[WVM_IDENTITY_ID_BYTES];
    uint8_t manifest_id[WVM_IDENTITY_ID_BYTES];
    struct wvm_vm_route_scope_key route_scope_key;
    enum wvm_lifecycle_state state;
    int has_candidate_manifest_digest;
    uint8_t candidate_manifest_digest[WVM_SHA256_DIGEST_BYTES];
    int has_prepared_route_snapshot_key;
    struct wvm_route_snapshot_key prepared_route_snapshot_key;
    int has_activation_record_digest;
    uint8_t activation_record_digest[WVM_SHA256_DIGEST_BYTES];
    uint64_t transaction_sequence;
};

int wvm_resource_reservation_derive(
    const struct wvm_reservation_requirement *requirement,
    const struct wvm_candidate_vm_manifest *candidate,
    const uint8_t candidate_manifest_digest[WVM_SHA256_DIGEST_BYTES],
    struct wvm_resource_reservation *reservation, uint64_t prepared_expiry_ms,
    char *error, size_t error_len);

int wvm_resource_reservation_validate(
    const struct wvm_resource_reservation *reservation, char *error,
    size_t error_len);
int wvm_resource_reservation_encode(
    const struct wvm_resource_reservation *reservation, uint8_t *bytes,
    size_t capacity, size_t *encoded_bytes, char *error, size_t error_len);
int wvm_resource_reservation_decode(
    const uint8_t *bytes, size_t encoded_bytes,
    struct wvm_resource_reservation *reservation, char *error,
    size_t error_len);

int wvm_resource_reservation_commit(
    struct wvm_resource_reservation *reservation,
    const struct wvm_activation_record *activation, char *error,
    size_t error_len);

int wvm_resource_reservation_begin_release(
    struct wvm_resource_reservation *reservation, char *error,
    size_t error_len);

int wvm_resource_reservation_release(
    struct wvm_resource_reservation *reservation, char *error,
    size_t error_len);

int wvm_activation_record_decide(
    struct wvm_activation_record *activation,
    const struct wvm_candidate_vm_manifest *candidate,
    const uint8_t candidate_manifest_digest[WVM_SHA256_DIGEST_BYTES],
    const uint8_t activation_fence[WVM_IDENTITY_ID_BYTES],
    uint64_t coordinator_instance_id,
    const uint8_t required_participant_set_digest[WVM_SHA256_DIGEST_BYTES],
    struct wvm_route_snapshot_key *required_route_snapshot_keys,
    size_t required_route_snapshot_count, size_t required_route_snapshot_capacity,
    enum wvm_activation_decision decision, uint64_t durable_decision_sequence,
    uint64_t decided_at, char *error, size_t error_len);

int wvm_activation_record_validate(const struct wvm_activation_record *activation,
                                   char *error, size_t error_len);
int wvm_activation_record_encode(
    const struct wvm_activation_record *activation, uint8_t *bytes,
    size_t capacity, size_t *encoded_bytes, char *error, size_t error_len);
int wvm_activation_record_decode(
    const uint8_t *bytes, size_t encoded_bytes,
    struct wvm_activation_record *activation, char *error, size_t error_len);

int wvm_startup_dependency_validate(
    const struct wvm_startup_dependency *dependency, char *error,
    size_t error_len);
int wvm_startup_dependency_encode(
    const struct wvm_startup_dependency *dependency, uint8_t *bytes,
    size_t capacity, size_t *encoded_bytes, char *error, size_t error_len);
int wvm_startup_dependency_decode(
    const uint8_t *bytes, size_t encoded_bytes,
    struct wvm_startup_dependency *dependency, char *error, size_t error_len);

int wvm_node_runtime_manifest_project(
    const struct wvm_candidate_vm_manifest *candidate,
    const uint8_t candidate_manifest_digest[WVM_SHA256_DIGEST_BYTES],
    const struct wvm_resource_reservation *reservation,
    const struct wvm_activation_record *activation, uint64_t local_role_bits,
    struct wvm_node_runtime_manifest *runtime_manifest, char *error,
    size_t error_len);

int wvm_node_runtime_manifest_validate(
    const struct wvm_node_runtime_manifest *runtime_manifest, char *error,
    size_t error_len);
int wvm_node_runtime_manifest_encode(
    const struct wvm_node_runtime_manifest *runtime_manifest, uint8_t *bytes,
    size_t capacity, size_t *encoded_bytes, char *error, size_t error_len);
int wvm_node_runtime_manifest_decode(
    const uint8_t *bytes, size_t encoded_bytes,
    struct wvm_node_runtime_manifest *runtime_manifest, char *error,
    size_t error_len);

int wvm_lifecycle_transaction_init(
    struct wvm_lifecycle_transaction *transaction,
    const struct wvm_candidate_vm_manifest *candidate,
    const uint8_t candidate_manifest_digest[WVM_SHA256_DIGEST_BYTES],
    char *error, size_t error_len);

int wvm_lifecycle_transition(struct wvm_lifecycle_transaction *transaction,
                             enum wvm_lifecycle_state expected,
                             enum wvm_lifecycle_state next, char *error,
                             size_t error_len);

int wvm_admission_transaction_record_validate(
    const struct wvm_admission_transaction_record *record, char *error,
    size_t error_len);
int wvm_admission_transaction_record_encode(
    const struct wvm_admission_transaction_record *record, uint8_t *bytes,
    size_t capacity, size_t *encoded_bytes, char *error, size_t error_len);
int wvm_admission_transaction_record_decode(
    const uint8_t *bytes, size_t encoded_bytes,
    struct wvm_admission_transaction_record *record, char *error,
    size_t error_len);

#endif /* WAVEVM_LIFECYCLE_H */
