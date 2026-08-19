#include "wavevm_reservation_runtime.h"

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

static int id_equal(const uint8_t left[WVM_IDENTITY_ID_BYTES],
                    const uint8_t right[WVM_IDENTITY_ID_BYTES])
{
    return memcmp(left, right, WVM_IDENTITY_ID_BYTES) == 0;
}

static uint64_t reservation_cpu(const struct wvm_resource_reservation *reservation)
{
    return (uint64_t)reservation->guest_vcpu_slots +
           reservation->overhead_vcpu_slots;
}

static uint64_t reservation_memory(
    const struct wvm_resource_reservation *reservation)
{
    return reservation->guest_memory_bytes + reservation->overhead_memory_bytes;
}

/* A replay must preserve the complete immutable lease record. */
static int lease_record_equal(const struct wvm_exclusive_lease *left,
                              const struct wvm_exclusive_lease *right)
{
    return left->lease_kind == right->lease_kind &&
           left->lease_generation == right->lease_generation &&
           strcmp(left->lease_name, right->lease_name) == 0;
}

/* Generation identifies an acquisition epoch, not a different local resource. */
static int lease_resource_equal(const struct wvm_exclusive_lease *left,
                                const struct wvm_exclusive_lease *right)
{
    return left->lease_kind == right->lease_kind &&
           strcmp(left->lease_name, right->lease_name) == 0;
}

static int reservation_equal(
    const struct wvm_resource_reservation *left,
    const struct wvm_resource_reservation *right)
{
    size_t i;

    if (!id_equal(left->reservation_id, right->reservation_id) ||
        memcmp(left->plan_digest, right->plan_digest,
               sizeof(left->plan_digest)) != 0 ||
        memcmp(left->candidate_manifest_digest,
               right->candidate_manifest_digest,
               sizeof(left->candidate_manifest_digest)) != 0 ||
        memcmp(left->admission_tx_id, right->admission_tx_id,
               sizeof(left->admission_tx_id)) != 0 ||
        memcmp(left->eligibility_fence_digest,
               right->eligibility_fence_digest,
               sizeof(left->eligibility_fence_digest)) != 0 ||
        left->vm_id != right->vm_id ||
        left->vm_incarnation != right->vm_incarnation ||
        left->physical_node_id != right->physical_node_id ||
        left->node_instance_id != right->node_instance_id ||
        left->inventory_revision != right->inventory_revision ||
        left->guest_vcpu_slots != right->guest_vcpu_slots ||
        left->guest_memory_bytes != right->guest_memory_bytes ||
        left->overhead_vcpu_slots != right->overhead_vcpu_slots ||
        left->overhead_memory_bytes != right->overhead_memory_bytes ||
        left->state != right->state ||
        left->has_prepared_expiry != right->has_prepared_expiry ||
        left->prepared_expiry_unix_time_ms !=
            right->prepared_expiry_unix_time_ms ||
        left->has_activation_fence != right->has_activation_fence ||
        memcmp(left->activation_fence, right->activation_fence,
               sizeof(left->activation_fence)) != 0 ||
        left->exclusive_leases.count != right->exclusive_leases.count) {
        return 0;
    }
    for (i = 0; i < left->exclusive_leases.count; i++) {
        if (!lease_record_equal(&left->exclusive_leases.entries[i],
                                &right->exclusive_leases.entries[i])) {
            return 0;
        }
    }
    return 1;
}

static struct wvm_resource_reservation *find_mutable(
    struct wvm_local_reservation_registry *registry,
    const uint8_t reservation_id[WVM_IDENTITY_ID_BYTES])
{
    size_t i;

    for (i = 0; i < registry->reservation_count; i++) {
        if (id_equal(registry->reservations[i].reservation_id,
                     reservation_id)) {
            return &registry->reservations[i];
        }
    }
    return NULL;
}

const struct wvm_resource_reservation *wvm_local_reservation_find(
    const struct wvm_local_reservation_registry *registry,
    const uint8_t reservation_id[WVM_IDENTITY_ID_BYTES])
{
    size_t i;

    if (!registry || !reservation_id) {
        return NULL;
    }
    for (i = 0; i < registry->reservation_count; i++) {
        if (id_equal(registry->reservations[i].reservation_id,
                     reservation_id)) {
            return &registry->reservations[i];
        }
    }
    return NULL;
}

static int leases_conflict(
    const struct wvm_local_reservation_registry *registry,
    const struct wvm_resource_reservation *candidate)
{
    size_t i;
    size_t j;

    for (i = 0; i < registry->reservation_count; i++) {
        const struct wvm_resource_reservation *existing =
            &registry->reservations[i];

        if (existing->state == WVM_RESERVATION_RELEASED ||
            id_equal(existing->reservation_id, candidate->reservation_id)) {
            continue;
        }
        for (j = 0; j < candidate->exclusive_leases.count; j++) {
            size_t k;

            for (k = 0; k < existing->exclusive_leases.count; k++) {
                if (lease_resource_equal(
                        &candidate->exclusive_leases.entries[j],
                        &existing->exclusive_leases.entries[k])) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int capacity_allows(
    const struct wvm_local_reservation_registry *registry,
    const struct wvm_resource_reservation *reservation)
{
    uint64_t used_cpu = (uint64_t)registry->prepared_vcpu_slots +
                        registry->committed_vcpu_slots;
    uint64_t used_memory = registry->prepared_memory_bytes +
                           registry->committed_memory_bytes;
    uint64_t cpu;
    uint64_t memory;

    if (reservation->overhead_vcpu_slots >
            UINT32_MAX - reservation->guest_vcpu_slots ||
        reservation->overhead_memory_bytes >
            UINT64_MAX - reservation->guest_memory_bytes ||
        used_cpu > registry->allocatable_vcpu_slots ||
        used_memory > registry->allocatable_memory_bytes) {
        return 0;
    }
    cpu = reservation_cpu(reservation);
    memory = reservation_memory(reservation);

    return cpu <= (uint64_t)registry->allocatable_vcpu_slots - used_cpu &&
           memory <= registry->allocatable_memory_bytes - used_memory;
}

static int reservation_account_units(
    const struct wvm_resource_reservation *reservation, uint32_t *cpu,
    uint64_t *memory)
{
    if (!reservation || !cpu || !memory ||
        reservation->overhead_vcpu_slots >
            UINT32_MAX - reservation->guest_vcpu_slots ||
        reservation->overhead_memory_bytes >
            UINT64_MAX - reservation->guest_memory_bytes) {
        return -1;
    }
    *cpu = reservation->guest_vcpu_slots + reservation->overhead_vcpu_slots;
    *memory = reservation->guest_memory_bytes +
              reservation->overhead_memory_bytes;
    return 0;
}

static int account_add(struct wvm_local_reservation_registry *registry,
                       const struct wvm_resource_reservation *reservation)
{
    uint32_t cpu;
    uint64_t memory;

    if (!registry || reservation_account_units(reservation, &cpu, &memory) !=
                         0) {
        return -1;
    }

    if (reservation->state == WVM_RESERVATION_PREPARED) {
        if (registry->prepared_vcpu_slots >
                UINT32_MAX - cpu ||
            registry->prepared_memory_bytes >
                UINT64_MAX - memory) {
            return -1;
        }
        registry->prepared_vcpu_slots += cpu;
        registry->prepared_memory_bytes += memory;
    } else if (reservation->state == WVM_RESERVATION_COMMITTED) {
        if (registry->committed_vcpu_slots >
                UINT32_MAX - cpu ||
            registry->committed_memory_bytes >
                UINT64_MAX - memory) {
            return -1;
        }
        registry->committed_vcpu_slots += cpu;
        registry->committed_memory_bytes += memory;
    }
    return 0;
}

static int account_remove(struct wvm_local_reservation_registry *registry,
                          const struct wvm_resource_reservation *reservation)
{
    uint32_t cpu;
    uint64_t memory;

    if (!registry || reservation_account_units(reservation, &cpu, &memory) !=
                         0) {
        return -1;
    }

    if (reservation->state == WVM_RESERVATION_PREPARED) {
        if (registry->prepared_vcpu_slots < cpu ||
            registry->prepared_memory_bytes < memory) {
            return -1;
        }
        registry->prepared_vcpu_slots -= cpu;
        registry->prepared_memory_bytes -= memory;
    } else if (reservation->state == WVM_RESERVATION_COMMITTED) {
        if (registry->committed_vcpu_slots < cpu ||
            registry->committed_memory_bytes < memory) {
            return -1;
        }
        registry->committed_vcpu_slots -= cpu;
        registry->committed_memory_bytes -= memory;
    }
    return 0;
}

static int account_remove_possible(
    const struct wvm_local_reservation_registry *registry,
    const struct wvm_resource_reservation *reservation)
{
    uint32_t cpu;
    uint64_t memory;

    if (!registry || reservation_account_units(reservation, &cpu, &memory) !=
                         0) {
        return -1;
    }
    if (reservation->state == WVM_RESERVATION_PREPARED) {
        return registry->prepared_vcpu_slots >= cpu &&
                       registry->prepared_memory_bytes >= memory
                   ? 0
                   : -1;
    }
    if (reservation->state == WVM_RESERVATION_COMMITTED) {
        return registry->committed_vcpu_slots >= cpu &&
                       registry->committed_memory_bytes >= memory
                   ? 0
                   : -1;
    }
    return 0;
}

int wvm_local_reservation_registry_init(
    struct wvm_local_reservation_registry *registry,
    const struct wvm_admission_node *node,
    struct wvm_resource_reservation *reservations,
    size_t reservation_capacity, char *error, size_t error_len)
{
    if (!registry || !node || !reservations || reservation_capacity == 0 ||
        node->physical_node_id == 0 || node->node_instance_id == 0 ||
        node->inventory_revision == 0 ||
        node->allocatable_memory_bytes == 0) {
        set_error(error, error_len, "reservation registry input is invalid");
        return -1;
    }
    memset(registry, 0, sizeof(*registry));
    pthread_mutex_init(&registry->lock, NULL);
    registry->physical_node_id = node->physical_node_id;
    registry->node_instance_id = node->node_instance_id;
    registry->inventory_revision = node->inventory_revision;
    registry->allocatable_vcpu_slots = node->allocatable_vcpu_slots;
    registry->allocatable_memory_bytes = node->allocatable_memory_bytes;
    registry->reservations = reservations;
    registry->reservation_capacity = reservation_capacity;
    memset(reservations, 0,
           reservation_capacity * sizeof(*registry->reservations));
    return 0;
}

void wvm_local_reservation_registry_destroy(
    struct wvm_local_reservation_registry *registry)
{
    if (!registry) {
        return;
    }
    pthread_mutex_destroy(&registry->lock);
    memset(registry, 0, sizeof(*registry));
}

int wvm_local_reservation_prepare(
    struct wvm_local_reservation_registry *registry,
    const struct wvm_resource_reservation *reservation,
    enum wvm_reservation_runtime_result *result, char *error,
    size_t error_len)
{
    struct wvm_resource_reservation *existing;
    size_t insertion_index;
    size_t i;

    if (!registry || !reservation ||
        wvm_resource_reservation_validate(reservation, error, error_len) != 0 ||
        reservation->state != WVM_RESERVATION_PREPARED ||
        reservation->physical_node_id != registry->physical_node_id ||
        reservation->node_instance_id != registry->node_instance_id ||
        reservation->inventory_revision != registry->inventory_revision) {
        set_error(error, error_len, "reservation prepare identity is invalid");
        return -1;
    }
    pthread_mutex_lock(&registry->lock);
    existing = find_mutable(registry, reservation->reservation_id);
    if (existing) {
        int equal = reservation_equal(existing, reservation);

        pthread_mutex_unlock(&registry->lock);
        if (!equal) {
            set_error(error, error_len,
                      "reservation ID is reused with different contents");
            return -1;
        }
        if (result) {
            *result = WVM_RESERVATION_RUNTIME_REPLAY;
        }
        return 0;
    }
    insertion_index = registry->reservation_count;
    for (i = 0; i < registry->reservation_count; i++) {
        if (registry->reservations[i].state == WVM_RESERVATION_RELEASED) {
            insertion_index = i;
            break;
        }
    }
    if (insertion_index == registry->reservation_capacity ||
        leases_conflict(registry, reservation) ||
        !capacity_allows(registry, reservation)) {
        pthread_mutex_unlock(&registry->lock);
        set_error(error, error_len,
                  "reservation conflicts with local capacity or lease");
        return -1;
    }
    registry->reservations[insertion_index] = *reservation;
    if (account_add(registry, reservation) != 0) {
        pthread_mutex_unlock(&registry->lock);
        set_error(error, error_len, "reservation accounting overflow");
        return -1;
    }
    if (insertion_index == registry->reservation_count) {
        registry->reservation_count++;
    }
    pthread_mutex_unlock(&registry->lock);
    if (result) {
        *result = WVM_RESERVATION_RUNTIME_NEW;
    }
    return 0;
}

int wvm_local_reservation_commit(
    struct wvm_local_reservation_registry *registry,
    const uint8_t reservation_id[WVM_IDENTITY_ID_BYTES],
    const struct wvm_activation_record *activation,
    enum wvm_reservation_runtime_result *result, char *error,
    size_t error_len)
{
    struct wvm_resource_reservation *reservation;
    uint32_t cpu;
    uint64_t memory;

    if (!registry || !reservation_id || !activation ||
        wvm_activation_record_validate(activation, error, error_len) != 0 ||
        activation->decision != WVM_ACTIVATION_ACTIVATE) {
        set_error(error, error_len, "reservation commit input is invalid");
        return -1;
    }
    pthread_mutex_lock(&registry->lock);
    reservation = find_mutable(registry, reservation_id);
    if (!reservation) {
        pthread_mutex_unlock(&registry->lock);
        set_error(error, error_len, "reservation is not prepared locally");
        return -1;
    }
    if (reservation->state == WVM_RESERVATION_COMMITTED) {
        int matches =
            memcmp(reservation->activation_fence, activation->activation_fence,
                   sizeof(reservation->activation_fence)) == 0;

        pthread_mutex_unlock(&registry->lock);
        if (!matches) {
            set_error(error, error_len,
                      "reservation already committed by another fence");
            return -1;
        }
        if (result) {
            *result = WVM_RESERVATION_RUNTIME_REPLAY;
        }
        return 0;
    }
    if (reservation->state != WVM_RESERVATION_PREPARED ||
        memcmp(reservation->admission_tx_id, activation->admission_tx_id,
               sizeof(reservation->admission_tx_id)) != 0 ||
        memcmp(reservation->candidate_manifest_digest,
               activation->candidate_manifest_digest,
               sizeof(reservation->candidate_manifest_digest)) != 0) {
        pthread_mutex_unlock(&registry->lock);
        set_error(error, error_len, "reservation commit does not match prepare");
        return -1;
    }
    if (reservation_account_units(reservation, &cpu, &memory) != 0 ||
        registry->prepared_vcpu_slots < cpu ||
        registry->prepared_memory_bytes < memory ||
        registry->committed_vcpu_slots > UINT32_MAX - cpu ||
        registry->committed_memory_bytes > UINT64_MAX - memory) {
        pthread_mutex_unlock(&registry->lock);
        set_error(error, error_len, "reservation accounting is inconsistent");
        return -1;
    }
    if (wvm_resource_reservation_commit(reservation, activation, error,
                                        error_len) != 0) {
        pthread_mutex_unlock(&registry->lock);
        set_error(error, error_len, "reservation commit does not match prepare");
        return -1;
    }
    registry->prepared_vcpu_slots -= cpu;
    registry->prepared_memory_bytes -= memory;
    registry->committed_vcpu_slots += cpu;
    registry->committed_memory_bytes += memory;
    pthread_mutex_unlock(&registry->lock);
    if (result) {
        *result = WVM_RESERVATION_RUNTIME_NEW;
    }
    return 0;
}

int wvm_local_reservation_abort(
    struct wvm_local_reservation_registry *registry,
    const uint8_t reservation_id[WVM_IDENTITY_ID_BYTES],
    enum wvm_reservation_runtime_result *result, char *error,
    size_t error_len)
{
    struct wvm_resource_reservation *reservation;
    struct wvm_resource_reservation accounted_reservation;

    if (!registry || !reservation_id) {
        set_error(error, error_len, "reservation abort input is invalid");
        return -1;
    }
    pthread_mutex_lock(&registry->lock);
    reservation = find_mutable(registry, reservation_id);
    if (!reservation || reservation->state == WVM_RESERVATION_RELEASED) {
        pthread_mutex_unlock(&registry->lock);
        if (result) {
            *result = WVM_RESERVATION_RUNTIME_REPLAY;
        }
        return 0;
    }
    accounted_reservation = *reservation;
    if (account_remove_possible(registry, &accounted_reservation) != 0) {
        pthread_mutex_unlock(&registry->lock);
        set_error(error, error_len, "reservation accounting is inconsistent");
        return -1;
    }
    if (wvm_resource_reservation_begin_release(reservation, error,
                                               error_len) != 0 ||
        wvm_resource_reservation_release(reservation, error, error_len) != 0) {
        pthread_mutex_unlock(&registry->lock);
        return -1;
    }
    if (account_remove(registry, &accounted_reservation) != 0) {
        pthread_mutex_unlock(&registry->lock);
        set_error(error, error_len, "reservation accounting is inconsistent");
        return -1;
    }
    pthread_mutex_unlock(&registry->lock);
    if (result) {
        *result = WVM_RESERVATION_RUNTIME_NEW;
    }
    return 0;
}

size_t wvm_local_reservation_reap_expired(
    struct wvm_local_reservation_registry *registry,
    uint64_t now_unix_time_ms)
{
    size_t i;
    size_t reaped = 0;

    if (!registry || now_unix_time_ms == 0) {
        return 0;
    }
    pthread_mutex_lock(&registry->lock);
    for (i = 0; i < registry->reservation_count; i++) {
        struct wvm_resource_reservation *reservation =
            &registry->reservations[i];

        if (reservation->state == WVM_RESERVATION_PREPARED &&
            reservation->has_prepared_expiry &&
            reservation->prepared_expiry_unix_time_ms <= now_unix_time_ms &&
            account_remove_possible(registry, reservation) == 0) {
            struct wvm_resource_reservation accounted_reservation = *reservation;

            if (wvm_resource_reservation_begin_release(reservation, NULL, 0) ==
                    0 &&
                wvm_resource_reservation_release(reservation, NULL, 0) == 0 &&
                account_remove(registry, &accounted_reservation) == 0) {
                reaped++;
            }
        }
    }
    pthread_mutex_unlock(&registry->lock);
    return reaped;
}
