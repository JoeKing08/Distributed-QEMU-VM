#pragma once

#include "qemu/osdep.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../../../common_include/wavevm_manifest.h"
#include "../../../common_include/wavevm_protocol.h"
#include "../../../common_include/wavevm_ioctl.h"

static inline int wavevm_qemu_runtime_gate_enabled(void)
{
    const char *value = getenv("WVM_RUNTIME_GATE_ACTIVE");

    return value && value[0] != '\0' && strcmp(value, "0") != 0;
}

static inline int wavevm_qemu_parse_u64_env(const char *name,
                                            uint64_t *value)
{
    const char *text = getenv(name);
    char *end = NULL;
    unsigned long long parsed;

    if (!text || !*text || !value) {
        return -1;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed == 0) {
        return -1;
    }
    *value = (uint64_t)parsed;
    return 0;
}

static inline int wavevm_qemu_parse_hex_env(
    const char *name, uint8_t *bytes, size_t byte_count)
{
    const char *text = getenv(name);
    size_t i;

    if (!text || strlen(text) != byte_count * 2U || !bytes) {
        return -1;
    }
    for (i = 0; i < byte_count; i++) {
        unsigned int byte;

        if (sscanf(text + i * 2U, "%2x", &byte) != 1 || byte > 0xffU) {
            return -1;
        }
        bytes[i] = (uint8_t)byte;
    }
    return 0;
}

static inline int wavevm_qemu_fill_runtime_registration(
    uint16_t ipc_role, struct wvm_ipc_runtime_registration *registration)
{
    uint64_t vm_incarnation;
    uint64_t manifest_generation;
    uint64_t local_runtime_instance_id;
    uint64_t value;
    const char *namespace_name;

    if (!registration ||
        wavevm_qemu_parse_u64_env("WVM_VM_ID", &value) != 0 ||
        value > UINT32_MAX) {
        return -1;
    }
    memset(registration, 0, sizeof(*registration));
    registration->magic = WVM_IPC_REGISTRATION_MAGIC;
    registration->version = WVM_IPC_REGISTRATION_VERSION;
    registration->ipc_role = ipc_role;
    registration->connection_role = WVM_MANIFEST_ROLE_QEMU_FRONTEND;
    registration->vm_id = (uint32_t)value;
    if (wavevm_qemu_parse_u64_env("WVM_VM_INCARNATION", &vm_incarnation) != 0 ||
        wavevm_qemu_parse_u64_env("WVM_MANIFEST_GENERATION",
                                  &manifest_generation) != 0 ||
        wavevm_qemu_parse_hex_env("WVM_CANDIDATE_MANIFEST_DIGEST",
                                  registration->candidate_manifest_digest,
                                  sizeof(registration->candidate_manifest_digest)) !=
            0 ||
        wavevm_qemu_parse_u64_env("WVM_RUNTIME_LOCAL_INSTANCE_ID",
                                  &local_runtime_instance_id) != 0 ||
        wavevm_qemu_parse_hex_env("WVM_CAPABILITY_PROFILE_DIGEST",
                                  registration->capability_profile_digest,
                                  sizeof(registration->capability_profile_digest)) !=
            0) {
        return -1;
    }
    registration->vm_incarnation = vm_incarnation;
    registration->manifest_generation = manifest_generation;
    registration->local_runtime_instance_id = local_runtime_instance_id;
    registration->caller_process_instance_id = (uint64_t)getpid();
    namespace_name = getenv("WVM_RUNTIME_NAMESPACE");
    if (!namespace_name || !*namespace_name ||
        strlen(namespace_name) >= sizeof(registration->requested_endpoint_name)) {
        return -1;
    }
    memcpy(registration->requested_endpoint_name, namespace_name,
           strlen(namespace_name) + 1U);
    return 0;
}

static inline int wavevm_qemu_query_kernel_caps(
    int dev_fd, struct wvm_ioctl_context_caps *caps)
{
    if (dev_fd < 0 || !caps) return -EINVAL;
    memset(caps, 0, sizeof(*caps));
    if (ioctl(dev_fd, IOCTL_WVM_QUERY_CAPS, caps) < 0) {
        return -errno;
    }
    if (caps->magic != WVM_KERNEL_CONTEXT_MAGIC ||
        caps->version != WVM_KERNEL_CONTEXT_ABI_VERSION ||
        caps->max_concurrent_contexts == 0 ||
        !(caps->feature_bits & WVM_KERNEL_CAP_CONTEXT_BIND)) {
        return -ENOTSUP;
    }
    return 0;
}

static inline bool wavevm_qemu_kernel_accel_usable(void)
{
    struct wvm_ioctl_context_caps caps;
    int dev_fd;
    int result;

    dev_fd = open("/dev/wavevm", O_RDWR);
    if (dev_fd < 0) return false;
    result = wavevm_qemu_query_kernel_caps(dev_fd, &caps);
    close(dev_fd);
    return result == 0;
}

/*
 * Mode A is admitted from the same manifest as the user-space runtime.  The
 * kernel module currently exposes one context per physical module instance;
 * binding here makes that limitation explicit instead of allowing a second
 * VM to overwrite module-global state.
 */
static inline int wavevm_qemu_bind_kernel_context(int dev_fd)
{
    struct wvm_ioctl_context_bind request;
    uint64_t value;

    if (!wavevm_qemu_runtime_gate_enabled()) return 0;
    if (dev_fd < 0 ||
        wavevm_qemu_parse_u64_env("WVM_VM_ID", &value) != 0 ||
        value == 0 || value > UINT32_MAX) {
        return -EINVAL;
    }

    memset(&request, 0, sizeof(request));
    request.magic = WVM_KERNEL_CONTEXT_MAGIC;
    request.version = WVM_KERNEL_CONTEXT_ABI_VERSION;
    request.vm_id = (uint32_t)value;
    if (wavevm_qemu_parse_u64_env("WVM_RUNTIME_PHYSICAL_NODE_ID",
                                  &value) != 0 ||
        value > UINT32_MAX) {
        return -EINVAL;
    }
    request.physical_node_id = (uint32_t)value;
    if (wavevm_qemu_parse_u64_env("WVM_VM_INCARNATION",
                                  &request.vm_incarnation) != 0 ||
        wavevm_qemu_parse_u64_env("WVM_MANIFEST_GENERATION",
                                  &request.manifest_generation) != 0 ||
        wavevm_qemu_parse_hex_env(
            "WVM_CANDIDATE_MANIFEST_DIGEST",
            request.candidate_manifest_digest,
            sizeof(request.candidate_manifest_digest)) != 0 ||
        wavevm_qemu_parse_hex_env(
            "WVM_CAPABILITY_PROFILE_DIGEST",
            request.capability_profile_digest,
            sizeof(request.capability_profile_digest)) != 0 ||
        wavevm_qemu_parse_hex_env("WVM_ACTIVATION_FENCE",
                                  request.activation_fence,
                                  sizeof(request.activation_fence)) != 0) {
        return -EINVAL;
    }
    if (wavevm_qemu_parse_u64_env("WVM_ROUTE_SCOPE_ID", &value) != 0 ||
        value == 0) {
        return -EINVAL;
    }
    request.route_snapshot_key.scope_key.route_scope_id = value;
    if (wavevm_qemu_parse_u64_env("WVM_TOPOLOGY_REVISION", &value) != 0 ||
        value == 0) {
        return -EINVAL;
    }
    request.route_snapshot_key.topology_revision = value;
    if (wavevm_qemu_parse_u64_env("WVM_ROUTE_GENERATION", &value) != 0 ||
        value == 0 ||
        wavevm_qemu_parse_hex_env(
            "WVM_ROUTE_SNAPSHOT_DIGEST",
            request.route_snapshot_key.snapshot_digest,
            sizeof(request.route_snapshot_key.snapshot_digest)) != 0) {
        return -EINVAL;
    }
    request.route_snapshot_key.route_generation = value;
    request.route_snapshot_key.scope_key.vm_id = request.vm_id;
    request.route_snapshot_key.scope_key.vm_incarnation =
        request.vm_incarnation;

    if (ioctl(dev_fd, IOCTL_WVM_BIND_CONTEXT, &request) < 0) {
        return -errno;
    }
    return 0;
}
