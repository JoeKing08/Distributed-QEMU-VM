#ifndef WAVEVM_ROUTE_DELIVERY_H
#define WAVEVM_ROUTE_DELIVERY_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_control.h"

#define WVM_ROUTE_DELIVERY_MAX_ENTRIES 65536U
#define WVM_ROUTE_DELIVERY_PATH_MAX 4096U

struct wvm_route_snapshot_file_storage {
    struct wvm_route_snapshot_record snapshot;
    struct wvm_route_rule_record *rules;
    struct wvm_required_ack_entry *ack_entries;
};

void wvm_route_snapshot_file_storage_init(
    struct wvm_route_snapshot_file_storage *storage);
void wvm_route_snapshot_file_storage_free(
    struct wvm_route_snapshot_file_storage *storage);

/*
 * The route artifact is a control-plane derived object.  For an admitted
 * manifest at PATH, the active route snapshot is PATH ".route".  Callers
 * may pass an explicit path when importing legacy fixtures, but production
 * launchers should use this derivation instead of an environment-only path.
 */
int wvm_route_snapshot_path_from_manifest(
    const char *manifest_path, char *route_path, size_t route_path_capacity,
    char *error, size_t error_len);

/*
 * Atomically publish one complete canonical snapshot.  The target is never
 * exposed partially: the writer fsyncs a private file and renames it into
 * place, then fsyncs the containing directory.
 */
int wvm_route_snapshot_file_publish(
    const char *path, const struct wvm_route_snapshot_record *snapshot,
    char *error, size_t error_len);

int wvm_route_snapshot_file_load(
    const char *path, struct wvm_route_snapshot_file_storage *storage,
    char *error, size_t error_len);

int wvm_route_snapshot_file_matches(
    const struct wvm_route_snapshot_file_storage *storage,
    const struct wvm_route_snapshot_key *expected_key, char *error,
    size_t error_len);

#endif
