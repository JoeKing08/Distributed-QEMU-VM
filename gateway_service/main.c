#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include "aggregator.h"
#include "../common_include/wavevm_protocol.h" 
#include "../common_include/wavevm_runtime_gate.h"

static struct wvm_runtime_manifest_storage g_gateway_manifest_storage;
static struct wvm_runtime_gate g_gateway_runtime_gate;

static int parse_gateway_u64(const char *text, uint64_t *value)
{
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

static int init_gateway_runtime_gate(void)
{
    const char *active = getenv("WVM_RUNTIME_GATE_ACTIVE");
    const char *manifest_path = getenv("WVM_RUNTIME_MANIFEST_PATH");
    const char *physical_id_text = getenv("WVM_RUNTIME_PHYSICAL_NODE_ID");
    const char *node_instance_text = getenv("WVM_NODE_INSTANCE_ID");
    uint64_t physical_id;
    uint64_t node_instance_id;
    uint8_t profile_digest[WVM_SHA256_DIGEST_BYTES];
    struct wvm_runtime_registration registration;
    uint64_t connection_id = 0;
    char error[256] = {0};

    if (!active || active[0] == '\0' || strcmp(active, "0") == 0) {
        return 0;
    }
    if (!manifest_path ||
        parse_gateway_u64(physical_id_text, &physical_id) != 0 ||
        parse_gateway_u64(node_instance_text, &node_instance_id) != 0 ||
        physical_id > UINT32_MAX) {
        fprintf(stderr, "[RuntimeGate] gateway identity environment is incomplete\n");
        return -1;
    }
    wvm_runtime_manifest_storage_init(&g_gateway_manifest_storage);
    wvm_runtime_gate_init(&g_gateway_runtime_gate);
    if (wvm_runtime_manifest_load_file(
            manifest_path, &g_gateway_manifest_storage, error,
            sizeof(error)) != 0 ||
        wvm_runtime_gate_prepare(
            &g_gateway_runtime_gate, &g_gateway_manifest_storage.manifest,
            (uint32_t)physical_id, node_instance_id, error, sizeof(error)) != 0 ||
        wvm_runtime_gate_activate(
            &g_gateway_runtime_gate,
            g_gateway_manifest_storage.manifest.activation_fence, error,
            sizeof(error)) != 0 ||
        wvm_runtime_manifest_profile_digest(
            &g_gateway_manifest_storage.manifest, profile_digest, error,
            sizeof(error)) != 0) {
        fprintf(stderr, "[RuntimeGate] gateway rejected manifest: %s\n",
                error[0] ? error : "manifest identity mismatch");
        wvm_runtime_manifest_storage_free(&g_gateway_manifest_storage);
        return -1;
    }

    memset(&registration, 0, sizeof(registration));
    registration.connection_role = WVM_MANIFEST_ROLE_GATEWAY;
    registration.vm_id = g_gateway_manifest_storage.manifest.vm_id;
    registration.vm_incarnation =
        g_gateway_manifest_storage.manifest.vm_incarnation;
    registration.manifest_generation =
        g_gateway_manifest_storage.manifest.manifest_generation;
    memcpy(registration.candidate_manifest_digest,
           g_gateway_manifest_storage.manifest.candidate_manifest_digest,
           sizeof(registration.candidate_manifest_digest));
    registration.local_runtime_instance_id = node_instance_id;
    registration.caller_process_instance_id = (uint64_t)getpid();
    memcpy(registration.capability_profile_digest, profile_digest,
           sizeof(registration.capability_profile_digest));
    snprintf(registration.requested_endpoint_name,
             sizeof(registration.requested_endpoint_name), "%s",
             g_gateway_manifest_storage.manifest.local_names.namespace_name);
    if (wvm_runtime_gate_register(&g_gateway_runtime_gate, &registration,
                                  &connection_id, error, sizeof(error)) != 0) {
        fprintf(stderr, "[RuntimeGate] gateway registration rejected: %s\n",
                error[0] ? error : "registration mismatch");
        wvm_runtime_manifest_storage_free(&g_gateway_manifest_storage);
        return -1;
    }
    fprintf(stderr,
            "[RuntimeGate] gateway registered connection=%" PRIu64 "\n",
            connection_id);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <LOCAL_PORT> <UPSTREAM_IP> <UPSTREAM_PORT> <CONFIG_FILE> <CTRL_PORT>\n", argv[0]);
        return 1;
    }

    int local = atoi(argv[1]);
    const char *up_ip = argv[2];
    int up_port = atoi(argv[3]);
    const char *conf = argv[4];

    g_ctrl_port = atoi(argv[5]);
    if (init_gateway_runtime_gate() != 0) {
        return 1;
    }

    printf("[*] WaveVM Gateway V16 (Chain Mode) | CtrlPort: %d\n", g_ctrl_port);
    
    if (init_aggregator(local, up_ip, up_port, conf) != 0) {
        fprintf(stderr, "[-] Init failed.\n");
        return 1;
    }

    while(1) {
        flush_all_buffers();
        usleep(1000); //太长会卡，太短烧 CPU
    }
    return 0;
}
