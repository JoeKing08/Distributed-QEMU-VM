#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../common_include/wavevm_config.h"
#include "../common_include/wavevm_ioctl.h"
#include "../common_include/wavevm_resources.h"

static int inject_route_table(int dev_fd, unsigned long request,
                              const uint32_t *table, uint32_t table_size,
                              const char *label)
{
    const uint32_t chunk_size = 1024;
    size_t payload_size =
        sizeof(struct wvm_ioctl_route_update) + chunk_size * sizeof(uint32_t);
    struct wvm_ioctl_route_update *payload = malloc(payload_size);

    if (!payload) {
        perror("malloc");
        return -1;
    }

    for (uint32_t i = 0; i < table_size; i += chunk_size) {
        uint32_t count = chunk_size;

        if (i + count > table_size) {
            count = table_size - i;
        }
        payload->start_index = i;
        payload->count = count;
        memcpy(payload->entries, &table[i], count * sizeof(uint32_t));
        if (ioctl(dev_fd, request, payload) < 0) {
            fprintf(stderr, "[FATAL] %s injection failed at %u (errno=%d)\n",
                    label, i, errno);
            free(payload);
            return -1;
        }
    }

    free(payload);
    return 0;
}

static int inject_global_param(int dev_fd, uint32_t slot, uint32_t value)
{
    size_t payload_size =
        sizeof(struct wvm_ioctl_route_update) + sizeof(uint32_t);
    struct wvm_ioctl_route_update *payload = malloc(payload_size);
    int ret;

    if (!payload) {
        perror("malloc");
        return -1;
    }
    payload->start_index = slot;
    payload->count = 1;
    payload->entries[0] = value;
    ret = ioctl(dev_fd, IOCTL_UPDATE_MEM_ROUTE, payload);
    free(payload);
    if (ret < 0) {
        perror("[-] Failed to inject global parameter");
        return -1;
    }
    return 0;
}

static int inject_vm_id(int dev_fd, uint8_t vm_id)
{
    if (ioctl(dev_fd, IOCTL_SET_VM_ID, &vm_id) < 0) {
        perror("[-] Failed to inject VM id");
        return -1;
    }
    return 0;
}

static int inject_gateway(int dev_fd, uint32_t id,
                          const struct wvm_resource_node *node)
{
    struct wvm_ioctl_gateway gateway = {
        .gw_id = id,
        .ip = inet_addr(node->ip),
        .port = htons(node->port),
    };

    if (ioctl(dev_fd, IOCTL_SET_GATEWAY, &gateway) < 0) {
        fprintf(stderr, "[-] gateway injection failed for id %u (errno=%d)\n",
                id, errno);
        return -1;
    }
    return 0;
}

static void build_legacy_cpu_table(const struct wvm_resource_plan *plan,
                                   uint32_t *table)
{
    uint32_t current_vcpu = 0;

    for (uint32_t i = 0; i < plan->node_count; i++) {
        for (uint32_t core = 0;
             core < plan->nodes[i].cpu_capacity &&
             current_vcpu < WVM_CPU_ROUTE_TABLE_SIZE;
             core++) {
            table[current_vcpu++] = plan->nodes[i].vnode_start;
        }
    }
    for (uint32_t cursor = 0; current_vcpu < WVM_CPU_ROUTE_TABLE_SIZE;
         cursor = (cursor + 1) % plan->node_count) {
        table[current_vcpu++] = plan->nodes[cursor].vnode_start;
    }
}

static void build_vm_tables(const struct wvm_resource_vm *vm,
                            uint32_t *cpu_table, uint32_t *memory_table)
{
    for (uint32_t i = 0; i < WVM_CPU_ROUTE_TABLE_SIZE; i++) {
        cpu_table[i] = WVM_NODE_AUTO_ROUTE;
    }
    for (uint32_t i = 0; i < WVM_MEMORY_ROUTE_TABLE_SIZE; i++) {
        memory_table[i] = WVM_NODE_AUTO_ROUTE;
    }
    for (uint32_t i = 0; i < vm->vcpu_count; i++) {
        cpu_table[i] = vm->vcpu_nodes[i];
    }
    for (uint32_t i = 0; i < vm->memory_chunk_count; i++) {
        memory_table[i] = vm->memory_nodes[i];
    }
}

int main(int argc, char **argv)
{
    struct wvm_resource_plan *plan;
    char error[256] = {0};
    const struct wvm_resource_node *my_node;
    const struct wvm_resource_vm *vm;
    uint32_t cpu_table[WVM_CPU_ROUTE_TABLE_SIZE];
    uint32_t memory_table[WVM_MEMORY_ROUTE_TABLE_SIZE];
    const char *config_file;
    int my_phys_id;
    int vm_id = 0;
    int dev_fd;

    plan = calloc(1, sizeof(*plan));
    if (!plan) {
        perror("calloc resource plan");
        return 1;
    }

    if (argc == 3 && strcmp(argv[1], "--plan") == 0) {
        if (wvm_resource_plan_load(argv[2], plan, error, sizeof(error)) != 0) {
            fprintf(stderr, "wvm_ctl: %s\n", error);
            free(plan);
            return 1;
        }
        wvm_resource_plan_print(plan, -1);
        free(plan);
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "--plan") == 0) {
        vm_id = atoi(argv[3]);
        if (vm_id < 0 || vm_id >= WVM_MAX_VMS ||
            wvm_resource_plan_load(argv[2], plan, error, sizeof(error)) != 0) {
            fprintf(stderr, "wvm_ctl: %s\n", error[0] ? error : "invalid VM id");
            free(plan);
            return 1;
        }
        wvm_resource_plan_print(plan, vm_id);
        free(plan);
        return 0;
    }
    if (argc < 3 || argc > 4) {
        fprintf(stderr,
                "Usage:\n"
                "  %s --plan <CONFIG> [VM_ID]\n"
                "  %s <CONFIG> <MY_PHYS_ID> [VM_ID]\n",
                argv[0], argv[0]);
        free(plan);
        return 1;
    }

    config_file = argv[1];
    my_phys_id = atoi(argv[2]);
    if (argc == 4) {
        vm_id = atoi(argv[3]);
    }
    if (vm_id < 0 || vm_id >= WVM_MAX_VMS) {
        fprintf(stderr, "wvm_ctl: VM_ID must be in [0, %u)\n", WVM_MAX_VMS);
        free(plan);
        return 1;
    }
    if (wvm_resource_plan_load(config_file, plan, error, sizeof(error)) != 0) {
        fprintf(stderr, "wvm_ctl: %s\n", error);
        free(plan);
        return 1;
    }

    my_node = wvm_resource_plan_find_node(plan, (uint32_t)my_phys_id);
    if (!my_node) {
        fprintf(stderr, "wvm_ctl: physical node %d is not in the config\n",
                my_phys_id);
        free(plan);
        return 1;
    }
    vm = wvm_resource_plan_get_vm(plan, (uint8_t)vm_id);
    if (plan->vm_count != 0 && !vm) {
        fprintf(stderr, "wvm_ctl: VM %d has no resource reservation\n", vm_id);
        free(plan);
        return 1;
    }

    if (vm) {
        build_vm_tables(vm, cpu_table, memory_table);
        if (!wvm_resource_plan_find_node(
                plan, wvm_resource_plan_host_node(plan, (uint8_t)vm_id))) {
            fprintf(stderr, "wvm_ctl: VM %d has no resolved host node\n", vm_id);
            free(plan);
            return 1;
        }
    } else {
        build_legacy_cpu_table(plan, cpu_table);
        for (uint32_t i = 0; i < WVM_MEMORY_ROUTE_TABLE_SIZE; i++) {
            memory_table[i] = WVM_NODE_AUTO_ROUTE;
        }
    }

    dev_fd = open("/dev/wavevm", O_RDWR);
    if (dev_fd < 0) {
        perror("[-] Failed to open /dev/wavevm");
        free(plan);
        return 1;
    }

    for (uint32_t i = 0; i < plan->node_count; i++) {
        const struct wvm_resource_node *node = &plan->nodes[i];

        for (uint32_t slot = 0; slot < node->dht_slots; slot++) {
            if (inject_gateway(dev_fd, node->vnode_start + slot, node) != 0) {
                close(dev_fd);
                free(plan);
                return 1;
            }
        }
    }
    if (inject_route_table(dev_fd, IOCTL_UPDATE_CPU_ROUTE, cpu_table,
                           WVM_CPU_ROUTE_TABLE_SIZE, "CPU route") != 0 ||
        inject_route_table(dev_fd, IOCTL_UPDATE_MEMORY_PLACEMENT, memory_table,
                           WVM_MEMORY_ROUTE_TABLE_SIZE,
                           "memory placement") != 0 ||
        inject_vm_id(dev_fd, (uint8_t)vm_id) != 0 ||
        inject_global_param(dev_fd, 0, plan->total_vnodes) != 0 ||
        inject_global_param(dev_fd, 1, my_node->vnode_start) != 0) {
        close(dev_fd);
        free(plan);
        return 1;
    }

    printf("[+] Configured: DHT slots=%u, node=%d vnode=%u, VM=%d%s\n",
           plan->total_vnodes, my_phys_id, my_node->vnode_start, vm_id,
           vm ? " (resource placement active)" : " (legacy placement)");
    close(dev_fd);
    free(plan);
    return 0;
}
