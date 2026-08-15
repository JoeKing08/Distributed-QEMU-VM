#include "wavevm_membership.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

static int bytes_are_zero(const uint8_t *bytes, size_t byte_count)
{
    size_t i;

    for (i = 0; i < byte_count; i++) {
        if (bytes[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static int member_key_equal(const struct wvm_member_key *left,
                            const struct wvm_member_key *right)
{
    return left->role_type == right->role_type &&
           left->role_id == right->role_id &&
           left->instance_id == right->instance_id;
}

static int member_key_compare(const struct wvm_member_key *left,
                              const struct wvm_member_key *right)
{
    if (left->role_type != right->role_type) {
        return left->role_type < right->role_type ? -1 : 1;
    }
    if (left->role_id != right->role_id) {
        return left->role_id < right->role_id ? -1 : 1;
    }
    if (left->instance_id != right->instance_id) {
        return left->instance_id < right->instance_id ? -1 : 1;
    }
    return 0;
}

static int route_scope_key_equal(const struct wvm_vm_route_scope_key *left,
                                 const struct wvm_vm_route_scope_key *right)
{
    return left->vm_id == right->vm_id &&
           left->vm_incarnation == right->vm_incarnation &&
           left->route_scope_id == right->route_scope_id;
}

static int route_snapshot_key_equal(
    const struct wvm_route_snapshot_key *left,
    const struct wvm_route_snapshot_key *right)
{
    return route_scope_key_equal(&left->scope_key, &right->scope_key) &&
           left->topology_revision == right->topology_revision &&
           left->route_generation == right->route_generation &&
           memcmp(left->snapshot_digest, right->snapshot_digest,
                  WVM_SHA256_DIGEST_BYTES) == 0;
}

static int dependency_equal(
    const struct wvm_membership_dependency *left,
    const struct wvm_membership_dependency *right)
{
    return member_key_equal(&left->member_key, &right->member_key) &&
           left->vm_id == right->vm_id &&
           left->vm_incarnation == right->vm_incarnation &&
           left->manifest_generation == right->manifest_generation &&
           left->dependency_kind == right->dependency_kind;
}

static int valid_health_state(enum wvm_membership_health_state health_state)
{
    return health_state >= WVM_MEMBERSHIP_HEALTHY &&
           health_state <= WVM_MEMBERSHIP_RECOVERING;
}

static int valid_member_kind(enum wvm_membership_member_kind kind)
{
    return kind == WVM_MEMBERSHIP_COMPUTE ||
           kind == WVM_MEMBERSHIP_GATEWAY;
}

static int valid_topology_kind(enum wvm_route_topology_kind topology_kind)
{
    return topology_kind == WVM_ROUTE_TOPOLOGY_FLAT ||
           topology_kind == WVM_ROUTE_TOPOLOGY_FRACTAL;
}

static int valid_transaction_state(enum wvm_route_transaction_state state)
{
    return state >= WVM_ROUTE_TRANSACTION_PREPARING &&
           state <= WVM_ROUTE_TRANSACTION_ABORTED;
}

static struct wvm_membership_member *
find_mutable(struct wvm_membership_registry *registry,
             const struct wvm_member_key *member_key)
{
    size_t i;

    if (!registry || !member_key) {
        return NULL;
    }
    for (i = 0; i < registry->member_count; i++) {
        if (member_key_equal(&registry->members[i].member_key, member_key)) {
            return &registry->members[i];
        }
    }
    return NULL;
}

const struct wvm_membership_member *
wvm_membership_find(const struct wvm_membership_registry *registry,
                    const struct wvm_member_key *member_key)
{
    size_t i;

    if (!registry || !member_key) {
        return NULL;
    }
    for (i = 0; i < registry->member_count; i++) {
        if (member_key_equal(&registry->members[i].member_key, member_key)) {
            return &registry->members[i];
        }
    }
    return NULL;
}

static void advance_membership_revision(struct wvm_membership_registry *registry,
                                        int topology_changed)
{
    registry->membership_revision++;
    registry->admission_eligibility_revision++;
    if (topology_changed) {
        registry->topology_revision++;
    }
}

static void stamp_member(struct wvm_membership_registry *registry,
                         struct wvm_membership_member *member)
{
    member->membership_revision = registry->membership_revision;
    member->topology_revision = registry->topology_revision;
}

void wvm_membership_registry_init(struct wvm_membership_registry *registry,
                                  struct wvm_membership_member *members,
                                  size_t member_capacity)
{
    if (!registry) {
        return;
    }
    memset(registry, 0, sizeof(*registry));
    registry->members = members;
    registry->member_capacity = member_capacity;
    registry->membership_revision = 1;
    registry->topology_revision = 1;
    registry->admission_eligibility_revision = 1;
    if (members && member_capacity != 0) {
        memset(members, 0, member_capacity * sizeof(*members));
    }
}

void wvm_membership_registry_set_dependencies(
    struct wvm_membership_registry *registry,
    struct wvm_membership_dependency *dependencies, size_t dependency_capacity)
{
    size_t i;

    if (!registry || registry->dependency_count != 0) {
        return;
    }
    registry->dependencies = dependencies;
    registry->dependency_capacity = dependency_capacity;
    if (dependencies && dependency_capacity != 0) {
        memset(dependencies, 0, dependency_capacity * sizeof(*dependencies));
    }
    for (i = 0; i < registry->member_count; i++) {
        registry->members[i].active_dependency_count = 0;
    }
}

int wvm_membership_member_validate(const struct wvm_membership_member *member,
                                   char *error, size_t error_len)
{
    if (!member || !valid_member_kind(member->kind) ||
        wvm_member_key_validate(&member->member_key, error, error_len) != 0 ||
        member->hosting_physical_node_id == 0 ||
        member->failure_domain_id == 0 ||
        wvm_capability_ref_validate(&member->capability, error, error_len) !=
            0 ||
        member->capability.physical_node_id !=
            member->hosting_physical_node_id ||
        !valid_health_state(member->health_state) ||
        member->desired_state < WVM_MANIFEST_MEMBER_PENDING ||
        member->desired_state > WVM_MANIFEST_MEMBER_FAILED) {
        set_error(error, error_len, "membership member has invalid metadata");
        return -1;
    }
    if ((member->kind == WVM_MEMBERSHIP_COMPUTE &&
         member->member_key.role_type != WVM_MANIFEST_ROLE_NODE_RUNTIME) ||
        (member->kind == WVM_MEMBERSHIP_GATEWAY &&
         member->member_key.role_type != WVM_MANIFEST_ROLE_GATEWAY)) {
        set_error(error, error_len, "membership member has wrong role kind");
        return -1;
    }
    return 0;
}

int wvm_membership_register(struct wvm_membership_registry *registry,
                            const struct wvm_membership_member *member,
                            char *error, size_t error_len)
{
    struct wvm_membership_member copy;
    size_t i;

    if (!registry || !registry->members ||
        registry->member_count == registry->member_capacity ||
        wvm_membership_member_validate(member, error, error_len) != 0) {
        set_error(error, error_len, "invalid membership registration");
        return -1;
    }
    for (i = 0; i < registry->member_count; i++) {
        const struct wvm_membership_member *existing = &registry->members[i];

        if (existing->member_key.role_type == member->member_key.role_type &&
            existing->member_key.role_id == member->member_key.role_id &&
            existing->desired_state != WVM_MANIFEST_MEMBER_REMOVED &&
            existing->desired_state != WVM_MANIFEST_MEMBER_FAILED) {
            set_error(error, error_len, "membership role is already registered");
            return -1;
        }
    }

    copy = *member;
    copy.desired_state = WVM_MANIFEST_MEMBER_PENDING;
    copy.health_state = WVM_MEMBERSHIP_RECOVERING;
    copy.active_dependency_count = 0;
    advance_membership_revision(registry, 1);
    stamp_member(registry, &copy);
    registry->members[registry->member_count++] = copy;
    return 0;
}

static int transition_member(struct wvm_membership_registry *registry,
                             const struct wvm_member_key *member_key,
                             enum wvm_manifest_member_state expected,
                             enum wvm_manifest_member_state next,
                             int topology_changed, char *error,
                             size_t error_len)
{
    struct wvm_membership_member *member = find_mutable(registry, member_key);

    if (!member || member->desired_state != expected) {
        set_error(error, error_len, "membership transition is not allowed");
        return -1;
    }
    member->desired_state = next;
    advance_membership_revision(registry, topology_changed);
    stamp_member(registry, member);
    return 0;
}

int wvm_membership_begin_validation(struct wvm_membership_registry *registry,
                                    const struct wvm_member_key *member_key,
                                    char *error, size_t error_len)
{
    return transition_member(registry, member_key, WVM_MANIFEST_MEMBER_PENDING,
                             WVM_MANIFEST_MEMBER_VALIDATING, 0, error,
                             error_len);
}

int wvm_membership_prepare(struct wvm_membership_registry *registry,
                           const struct wvm_member_key *member_key,
                           char *error, size_t error_len)
{
    return transition_member(registry, member_key,
                             WVM_MANIFEST_MEMBER_VALIDATING,
                             WVM_MANIFEST_MEMBER_PREPARED, 0, error,
                             error_len);
}

int wvm_membership_activate(struct wvm_membership_registry *registry,
                            const struct wvm_member_key *member_key,
                            char *error, size_t error_len)
{
    (void)registry;
    (void)member_key;
    set_error(error, error_len,
              "membership activation requires a committed route transaction");
    return -1;
}

static const struct wvm_route_ack_entry *find_transaction_ack(
    const struct wvm_route_transaction *transaction,
    const struct wvm_member_key *member_key)
{
    size_t i;

    if (!transaction || !member_key) {
        return NULL;
    }
    for (i = 0; i < transaction->required_ack_set.count; i++) {
        const struct wvm_route_ack_entry *entry =
            &transaction->required_ack_set.entries[i];

        if (member_key_equal(&entry->member_key, member_key)) {
            return entry;
        }
    }
    return NULL;
}

static const struct wvm_route_ack_entry *find_ack_in_set(
    const struct wvm_route_ack_set *ack_set,
    const struct wvm_member_key *member_key)
{
    size_t i;

    if (!ack_set || !member_key) {
        return NULL;
    }
    for (i = 0; i < ack_set->count; i++) {
        if (member_key_equal(&ack_set->entries[i].member_key, member_key)) {
            return &ack_set->entries[i];
        }
    }
    return NULL;
}

int wvm_membership_activate_with_route(
    struct wvm_membership_registry *registry,
    const struct wvm_member_key *member_key,
    const struct wvm_route_transaction *transaction, char *error,
    size_t error_len)
{
    struct wvm_membership_member *member;
    const struct wvm_route_ack_entry *ack;

    if (!registry || !member_key || !transaction ||
        transaction->state != WVM_ROUTE_TRANSACTION_ACTIVATED ||
        !transaction->successor) {
        set_error(error, error_len,
                  "membership activation transaction is not committed");
        return -1;
    }
    member = find_mutable(registry, member_key);
    ack = find_transaction_ack(transaction, member_key);
    if (!member || !ack || !ack->prepared || !ack->activated ||
        !route_snapshot_key_equal(&ack->expected_snapshot_key,
                                  &transaction->successor->key) ||
        member->health_state != WVM_MEMBERSHIP_HEALTHY) {
        set_error(error, error_len,
                  "membership member is not authorized by route transaction");
        return -1;
    }
    if (member->desired_state == WVM_MANIFEST_MEMBER_ACTIVE) {
        return 0;
    }
    if (member->desired_state != WVM_MANIFEST_MEMBER_PREPARED) {
        set_error(error, error_len, "membership member is not prepared");
        return -1;
    }
    member->desired_state = WVM_MANIFEST_MEMBER_ACTIVE;
    advance_membership_revision(registry, 1);
    stamp_member(registry, member);
    return 0;
}

int wvm_membership_cordon(struct wvm_membership_registry *registry,
                          const struct wvm_member_key *member_key,
                          char *error, size_t error_len)
{
    struct wvm_membership_member *member = find_mutable(registry, member_key);

    if (!member || member->desired_state != WVM_MANIFEST_MEMBER_ACTIVE) {
        set_error(error, error_len, "membership member cannot be cordoned");
        return -1;
    }
    member->desired_state = WVM_MANIFEST_MEMBER_CORDONED;
    advance_membership_revision(registry, 0);
    stamp_member(registry, member);
    return 0;
}

int wvm_membership_begin_drain(struct wvm_membership_registry *registry,
                               const struct wvm_member_key *member_key,
                               char *error, size_t error_len)
{
    struct wvm_membership_member *member = find_mutable(registry, member_key);

    if (!member ||
        (member->desired_state != WVM_MANIFEST_MEMBER_ACTIVE &&
         member->desired_state != WVM_MANIFEST_MEMBER_CORDONED) ||
        member->active_dependency_count != 0) {
        set_error(error, error_len,
                  "membership member cannot drain while dependencies exist");
        return -1;
    }
    member->desired_state = WVM_MANIFEST_MEMBER_DRAINING;
    advance_membership_revision(registry, 1);
    stamp_member(registry, member);
    return 0;
}

int wvm_membership_remove(struct wvm_membership_registry *registry,
                          const struct wvm_member_key *member_key,
                          char *error, size_t error_len)
{
    return transition_member(registry, member_key,
                             WVM_MANIFEST_MEMBER_DRAINING,
                             WVM_MANIFEST_MEMBER_REMOVED, 1, error,
                             error_len);
}

int wvm_membership_mark_health(struct wvm_membership_registry *registry,
                               const struct wvm_member_key *member_key,
                               enum wvm_membership_health_state health_state,
                               char *error, size_t error_len)
{
    struct wvm_membership_member *member = find_mutable(registry, member_key);

    if (!member || !valid_health_state(health_state) ||
        member->desired_state == WVM_MANIFEST_MEMBER_REMOVED ||
        member->desired_state == WVM_MANIFEST_MEMBER_FAILED) {
        set_error(error, error_len, "invalid membership health update");
        return -1;
    }
    if (member->health_state == health_state) {
        return 0;
    }
    member->health_state = health_state;
    advance_membership_revision(registry, 0);
    stamp_member(registry, member);
    return 0;
}

static int dependency_validate(
    const struct wvm_membership_dependency *dependency, char *error,
    size_t error_len)
{
    if (!dependency ||
        wvm_member_key_validate(&dependency->member_key, error, error_len) !=
            0 ||
        dependency->vm_id == 0 || dependency->vm_incarnation == 0 ||
        dependency->manifest_generation == 0 ||
        dependency->dependency_kind == 0) {
        set_error(error, error_len, "membership dependency is invalid");
        return -1;
    }
    return 0;
}

int wvm_membership_dependency_acquire(
    struct wvm_membership_registry *registry,
    const struct wvm_membership_dependency *dependency, char *error,
    size_t error_len)
{
    struct wvm_membership_member *member;
    size_t i;

    if (!registry ||
        dependency_validate(dependency, error, error_len) != 0) {
        return -1;
    }
    for (i = 0; i < registry->dependency_count; i++) {
        if (dependency_equal(&registry->dependencies[i], dependency)) {
            return 0;
        }
    }
    member = find_mutable(registry, &dependency->member_key);
    if (!registry->dependencies || registry->dependency_capacity == 0 ||
        registry->dependency_count == registry->dependency_capacity || !member ||
        !wvm_membership_is_admission_eligible(member) ||
        member->active_dependency_count == UINT64_MAX) {
        set_error(error, error_len, "membership dependency cannot be acquired");
        return -1;
    }
    registry->dependencies[registry->dependency_count++] = *dependency;
    member->active_dependency_count++;
    return 0;
}

int wvm_membership_dependency_release(
    struct wvm_membership_registry *registry,
    const struct wvm_membership_dependency *dependency, char *error,
    size_t error_len)
{
    struct wvm_membership_member *member;
    size_t i;

    if (!registry ||
        dependency_validate(dependency, error, error_len) != 0) {
        return -1;
    }
    for (i = 0; i < registry->dependency_count; i++) {
        if (dependency_equal(&registry->dependencies[i], dependency)) {
            break;
        }
    }
    if (i == registry->dependency_count) {
        return 0;
    }
    member = find_mutable(registry, &dependency->member_key);
    if (!member || member->active_dependency_count == 0) {
        set_error(error, error_len, "membership dependency accounting is invalid");
        return -1;
    }
    member->active_dependency_count--;
    if (i + 1U < registry->dependency_count) {
        memmove(&registry->dependencies[i], &registry->dependencies[i + 1U],
                (registry->dependency_count - i - 1U) *
                    sizeof(*registry->dependencies));
    }
    registry->dependency_count--;
    memset(&registry->dependencies[registry->dependency_count], 0,
           sizeof(*registry->dependencies));
    return 0;
}

int wvm_membership_is_admission_eligible(
    const struct wvm_membership_member *member)
{
    return member && member->desired_state == WVM_MANIFEST_MEMBER_ACTIVE &&
           member->health_state == WVM_MEMBERSHIP_HEALTHY;
}

static int route_rule_compare(const struct wvm_route_rule *left,
                              const struct wvm_route_rule *right)
{
    if (left->destination_kind != right->destination_kind) {
        return left->destination_kind < right->destination_kind ? -1 : 1;
    }
    if (left->destination_scope != right->destination_scope) {
        return left->destination_scope < right->destination_scope ? -1 : 1;
    }
    if (left->destination_vnode_or_endpoint !=
        right->destination_vnode_or_endpoint) {
        return left->destination_vnode_or_endpoint <
                       right->destination_vnode_or_endpoint
                   ? -1
                   : 1;
    }
    return 0;
}

static int member_is_transaction_route_eligible(
    const struct wvm_membership_registry *registry,
    const struct wvm_member_key *member_key,
    const struct wvm_route_transaction *transaction)
{
    size_t i;
    const struct wvm_membership_member *member;

    member = wvm_membership_find(registry, member_key);
    if (wvm_membership_is_admission_eligible(member)) {
        return 1;
    }
    if (!member || member->desired_state != WVM_MANIFEST_MEMBER_PREPARED ||
        member->health_state != WVM_MEMBERSHIP_HEALTHY || !transaction ||
        transaction->state != WVM_ROUTE_TRANSACTION_PREPARING) {
        return 0;
    }
    for (i = 0; i < transaction->required_ack_set.count; i++) {
        const struct wvm_route_ack_entry *entry =
            &transaction->required_ack_set.entries[i];

        if (member_key_equal(&entry->member_key, member_key) &&
            entry->prepared &&
            route_snapshot_key_equal(&entry->expected_snapshot_key,
                                     &transaction->successor->key)) {
            return 1;
        }
    }
    return 0;
}

static int route_ack_sets_match(
    const struct wvm_route_ack_set *snapshot_ack_set,
    const struct wvm_route_ack_set *transaction_ack_set,
    const struct wvm_route_snapshot_key *expected_key)
{
    size_t i;

    if (!snapshot_ack_set || !transaction_ack_set ||
        snapshot_ack_set->count != transaction_ack_set->count ||
        memcmp(snapshot_ack_set->entries_digest,
               transaction_ack_set->entries_digest,
               WVM_SHA256_DIGEST_BYTES) != 0) {
        return 0;
    }
    for (i = 0; i < snapshot_ack_set->count; i++) {
        const struct wvm_route_ack_entry *snapshot_entry =
            &snapshot_ack_set->entries[i];
        const struct wvm_route_ack_entry *transaction_entry =
            find_ack_in_set(transaction_ack_set, &snapshot_entry->member_key);

        if (!transaction_entry ||
            !route_snapshot_key_equal(&snapshot_entry->expected_snapshot_key,
                                      expected_key) ||
            !route_snapshot_key_equal(&transaction_entry->expected_snapshot_key,
                                      expected_key) ||
            !transaction_entry->prepared) {
            return 0;
        }
    }
    return 1;
}

static int route_snapshot_validate_internal(
    const struct wvm_route_snapshot *snapshot,
    const struct wvm_membership_registry *registry,
    const struct wvm_route_transaction *transaction, char *error,
    size_t error_len)
{
    size_t i;

    if (!snapshot || !registry ||
        wvm_route_snapshot_key_validate(&snapshot->key, error, error_len) != 0 ||
        snapshot->membership_revision != registry->membership_revision ||
        !valid_topology_kind(snapshot->topology_kind) ||
        !snapshot->rules || snapshot->rule_count == 0 ||
        snapshot->rule_count > snapshot->rule_capacity ||
        !snapshot->required_ack_set ||
        snapshot->required_ack_set->count == 0 ||
        snapshot->required_ack_set->count >
            snapshot->required_ack_set->capacity ||
        bytes_are_zero(snapshot->required_ack_set->entries_digest,
                       sizeof(snapshot->required_ack_set->entries_digest)) ||
        snapshot->operation_retention_horizon_ms == 0 ||
        snapshot->retirement_policy == 0 ||
        (snapshot->has_predecessor &&
         !route_scope_key_equal(&snapshot->predecessor_key.scope_key,
                                &snapshot->key.scope_key))) {
        set_error(error, error_len, "route snapshot has invalid metadata");
        return -1;
    }
    if (snapshot->key.topology_revision != registry->topology_revision) {
        set_error(error, error_len, "route snapshot has stale topology revision");
        return -1;
    }
    for (i = 0; i < snapshot->rule_count; i++) {
        const struct wvm_route_rule *rule = &snapshot->rules[i];

        if (rule->destination_kind == 0 || rule->next_hop_kind == 0 ||
            rule->hop_limit == 0 ||
            wvm_member_key_validate(&rule->next_hop_member, error,
                                    error_len) != 0 ||
            !(transaction
                  ? member_is_transaction_route_eligible(
                        registry, &rule->next_hop_member, transaction)
                  : wvm_membership_is_admission_eligible(
                        wvm_membership_find(registry,
                                            &rule->next_hop_member))) ||
            (i != 0 &&
             route_rule_compare(&snapshot->rules[i - 1], rule) >= 0)) {
            set_error(error, error_len, "route snapshot has invalid rules");
            return -1;
        }
    }
    for (i = 0; i < snapshot->required_ack_set->count; i++) {
        const struct wvm_route_ack_entry *entry =
            &snapshot->required_ack_set->entries[i];

        if (wvm_member_key_validate(&entry->member_key, error, error_len) !=
                0 ||
            !route_snapshot_key_equal(&entry->expected_snapshot_key,
                                      &snapshot->key) ||
            !(transaction
                  ? member_is_transaction_route_eligible(
                        registry, &entry->member_key, transaction)
                  : wvm_membership_is_admission_eligible(
                        wvm_membership_find(registry, &entry->member_key))) ||
            (i != 0 &&
             member_key_compare(&snapshot->required_ack_set->entries[i - 1]
                                     .member_key,
                                &entry->member_key) >= 0)) {
            set_error(error, error_len,
                      "route snapshot has invalid required acknowledgements");
            return -1;
        }
    }
    return 0;
}

int wvm_route_snapshot_validate(const struct wvm_route_snapshot *snapshot,
                                const struct wvm_membership_registry *registry,
                                char *error, size_t error_len)
{
    return route_snapshot_validate_internal(snapshot, registry, NULL, error,
                                            error_len);
}

int wvm_route_transaction_begin(
    struct wvm_route_transaction *transaction,
    const uint8_t operation_id[WVM_IDENTITY_ID_BYTES],
    const struct wvm_route_snapshot *successor,
    const struct wvm_route_snapshot_key *predecessor_key,
    struct wvm_route_ack_entry *required_ack_entries,
    size_t required_ack_capacity, size_t required_ack_count,
    const uint8_t required_ack_set_digest[WVM_SHA256_DIGEST_BYTES],
    char *error, size_t error_len)
{
    if (!transaction || !operation_id || !successor || !required_ack_entries ||
        required_ack_count == 0 || required_ack_count > required_ack_capacity ||
        !required_ack_set_digest || bytes_are_zero(operation_id,
                                                    WVM_IDENTITY_ID_BYTES) ||
        bytes_are_zero(required_ack_set_digest, WVM_SHA256_DIGEST_BYTES) ||
        (predecessor_key &&
         !route_scope_key_equal(&predecessor_key->scope_key,
                                &successor->key.scope_key))) {
        set_error(error, error_len, "invalid route transaction");
        return -1;
    }
    memset(transaction, 0, sizeof(*transaction));
    memcpy(transaction->operation_id, operation_id,
           sizeof(transaction->operation_id));
    transaction->successor = successor;
    transaction->has_predecessor = predecessor_key != NULL;
    if (predecessor_key) {
        transaction->predecessor_key = *predecessor_key;
    }
    transaction->required_ack_set.entries = required_ack_entries;
    transaction->required_ack_set.count = required_ack_count;
    transaction->required_ack_set.capacity = required_ack_capacity;
    memcpy(transaction->required_ack_set.entries_digest,
           required_ack_set_digest,
           sizeof(transaction->required_ack_set.entries_digest));
    transaction->operation_retention_horizon_ms =
        successor->operation_retention_horizon_ms;
    transaction->state = WVM_ROUTE_TRANSACTION_PREPARING;
    return 0;
}

int wvm_route_transaction_ack_prepare(
    struct wvm_route_transaction *transaction,
    const struct wvm_member_key *member_key,
    const struct wvm_route_snapshot_key *snapshot_key,
    char *error, size_t error_len)
{
    size_t i;

    if (!transaction || !member_key || !snapshot_key ||
        transaction->state != WVM_ROUTE_TRANSACTION_PREPARING ||
        !route_snapshot_key_equal(snapshot_key, &transaction->successor->key)) {
        set_error(error, error_len, "route prepare ACK is invalid");
        return -1;
    }
    for (i = 0; i < transaction->required_ack_set.count; i++) {
        struct wvm_route_ack_entry *entry =
            &transaction->required_ack_set.entries[i];

        if (member_key_equal(&entry->member_key, member_key)) {
            if (!route_snapshot_key_equal(&entry->expected_snapshot_key,
                                          snapshot_key)) {
                set_error(error, error_len, "route prepare ACK has wrong key");
                return -1;
            }
            entry->prepared = 1;
            return 0;
        }
    }
    set_error(error, error_len, "route prepare ACK is from a non-member");
    return -1;
}

int wvm_route_transaction_commit(
    struct wvm_route_transaction *transaction,
    const struct wvm_membership_registry *registry, char *error,
    size_t error_len)
{
    size_t i;

    if (!transaction || transaction->state != WVM_ROUTE_TRANSACTION_PREPARING ||
        !transaction->successor ||
        !route_ack_sets_match(transaction->successor->required_ack_set,
                              &transaction->required_ack_set,
                              &transaction->successor->key) ||
        route_snapshot_validate_internal(transaction->successor, registry,
                                         transaction, error, error_len) != 0) {
        set_error(error, error_len, "route transaction cannot commit");
        return -1;
    }
    for (i = 0; i < transaction->required_ack_set.count; i++) {
        struct wvm_route_ack_entry *entry =
            &transaction->required_ack_set.entries[i];

        if (!entry->prepared) {
            set_error(error, error_len, "route transaction is missing an ACK");
            return -1;
        }
        entry->activated = 1;
    }
    transaction->state = WVM_ROUTE_TRANSACTION_ACTIVATED;
    return 0;
}

int wvm_route_transaction_begin_retire(
    struct wvm_route_transaction *transaction, uint64_t active_operation_refs,
    int retention_horizon_complete, char *error, size_t error_len)
{
    if (!transaction || transaction->state != WVM_ROUTE_TRANSACTION_ACTIVATED ||
        active_operation_refs != 0 || !retention_horizon_complete) {
        set_error(error, error_len,
                  "route transaction cannot retire before drain completes");
        return -1;
    }
    transaction->state = WVM_ROUTE_TRANSACTION_RETIRING;
    return 0;
}

int wvm_route_transaction_retire(struct wvm_route_transaction *transaction,
                                 char *error, size_t error_len)
{
    if (!transaction || transaction->state != WVM_ROUTE_TRANSACTION_RETIRING) {
        set_error(error, error_len, "route transaction is not retiring");
        return -1;
    }
    transaction->state = WVM_ROUTE_TRANSACTION_RETIRED;
    return 0;
}

int wvm_route_transaction_abort(struct wvm_route_transaction *transaction,
                                char *error, size_t error_len)
{
    if (!transaction || !valid_transaction_state(transaction->state) ||
        transaction->state == WVM_ROUTE_TRANSACTION_ACTIVATED ||
        transaction->state == WVM_ROUTE_TRANSACTION_RETIRING ||
        transaction->state == WVM_ROUTE_TRANSACTION_RETIRED) {
        set_error(error, error_len, "route transaction cannot abort");
        return -1;
    }
    transaction->state = WVM_ROUTE_TRANSACTION_ABORTED;
    return 0;
}
