/*
 * [IDENTITY] Main Wrapper - The Identity Mapper
 * ---------------------------------------------------------------------------
 * 物理角色：Daemon 的启动引擎与"身份翻译官"。
 * 职责边界：
 * 1. 解析 swarm_config，将物理资源容量、DHT 权重和 VM placement 分离。
 * 2. 初始化共享内存后端，建立 QEMU 与 Daemon 的 IPC 桥梁。
 * 3. 启动自治监控线程 (Gossip)，驱动节点生命周期演进。
 * 
 * [禁止事项]
 * - 严禁在未显式配置 dht_slots 时改变兼容的 RAM/4GB DHT slot 规则。
 * - 严禁在 QEMU 建立连接前提前释放资源。
 * ---------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>

#include "logic_core.h"
#include "../common_include/wavevm_protocol.h"
#include "../common_include/wavevm_ioctl.h"
#include "../common_include/wavevm_config.h"
#include "../common_include/wavevm_resources.h"

// --- 全局状态 ---
extern struct dsm_driver_ops u_ops;
extern int user_backend_init(int my_node_id, int port);
void *g_shm_ptr = NULL;
size_t g_shm_size = 0;
int g_dev_fd = -1;
extern int g_my_node_id;
uint8_t g_my_vm_id = 0;  // Multi-VM: 默认 0，向后兼容
static struct wvm_resource_plan g_resource_plan;
static int g_resource_plan_ready = 0;

#define MAX_QEMU_CLIENTS 8

static void inject_cpu_route_table(void) {
    if (g_dev_fd < 0) return;
    const uint32_t *table = wvm_get_cpu_route_table();
    if (!table) return;

    const uint32_t chunk_size = 1024;
    size_t buf_size = sizeof(struct wvm_ioctl_route_update) + chunk_size * sizeof(uint32_t);
    struct wvm_ioctl_route_update *payload = malloc(buf_size);
    if (!payload) {
        fprintf(stderr, "[CPU-ROUTE] malloc failed\n");
        return;
    }

    for (uint32_t i = 0; i < WVM_CPU_ROUTE_TABLE_SIZE; i += chunk_size) {
        uint32_t count = chunk_size;
        if (i + count > WVM_CPU_ROUTE_TABLE_SIZE) count = WVM_CPU_ROUTE_TABLE_SIZE - i;

        payload->start_index = i;
        payload->count = count;
        memcpy(payload->entries, &table[i], count * sizeof(uint32_t));

        if (ioctl(g_dev_fd, IOCTL_UPDATE_CPU_ROUTE, payload) < 0) {
            fprintf(stderr, "[CPU-ROUTE] inject failed at %u (errno=%d)\n", i, errno);
            free(payload);
            return;
        }
    }

    fprintf(stderr, "[CPU-ROUTE] injected %u entries\n", (unsigned)WVM_CPU_ROUTE_TABLE_SIZE);
    free(payload);
}

static void inject_memory_route_table(void) {
    const uint32_t *table;
    const uint32_t chunk_size = 1024;
    size_t buf_size;
    struct wvm_ioctl_route_update *payload;

    if (g_dev_fd < 0) return;
    table = wvm_get_memory_route_table();
    if (!table) return;

    buf_size = sizeof(*payload) + chunk_size * sizeof(uint32_t);
    payload = malloc(buf_size);
    if (!payload) {
        fprintf(stderr, "[MEM-ROUTE] malloc failed\n");
        return;
    }

    for (uint32_t i = 0; i < WVM_MEMORY_ROUTE_TABLE_SIZE; i += chunk_size) {
        uint32_t count = chunk_size;

        if (i + count > WVM_MEMORY_ROUTE_TABLE_SIZE) {
            count = WVM_MEMORY_ROUTE_TABLE_SIZE - i;
        }
        payload->start_index = i;
        payload->count = count;
        memcpy(payload->entries, &table[i], count * sizeof(uint32_t));
        if (ioctl(g_dev_fd, IOCTL_UPDATE_MEMORY_PLACEMENT, payload) < 0) {
            fprintf(stderr, "[MEM-ROUTE] inject failed at %u (errno=%d)\n",
                    i, errno);
            free(payload);
            return;
        }
    }

    fprintf(stderr, "[MEM-ROUTE] injected %u entries\n",
            (unsigned)WVM_MEMORY_ROUTE_TABLE_SIZE);
    free(payload);
}

static void inject_mem_global(uint32_t slot, uint32_t value) {
    size_t buf_size = sizeof(struct wvm_ioctl_route_update) + sizeof(uint32_t);
    struct wvm_ioctl_route_update *payload;

    if (g_dev_fd < 0) return;
    payload = malloc(buf_size);
    if (!payload) {
        fprintf(stderr, "[MEM-GLOBAL] malloc failed\n");
        return;
    }
    payload->start_index = slot;
    payload->count = 1;
    payload->entries[0] = value;
    if (ioctl(g_dev_fd, IOCTL_UPDATE_MEM_ROUTE, payload) < 0) {
        fprintf(stderr, "[MEM-GLOBAL] inject slot %u failed (errno=%d)\n",
                slot, errno);
    }
    free(payload);
}

static void inject_vm_id(uint8_t vm_id) {
    if (g_dev_fd < 0) return;
    if (ioctl(g_dev_fd, IOCTL_SET_VM_ID, &vm_id) < 0) {
        fprintf(stderr, "[VM-ID] inject failed (errno=%d)\n", errno);
    }
}
#define NUM_BCAST_WORKERS 8

/* [FIX-G2] 坚如磐石的循环读取，处理 Partial Read 和 EINTR */
static ssize_t read_exact(int fd, void *buf, size_t len) {
    size_t received = 0;
    char *ptr = (char *)buf;
    while (received < len) {
        ssize_t ret = read(fd, ptr + received, len - received);
        if (ret > 0) {
            received += ret;
        } else if (ret == 0) {
            return -1; // EOF: 对端关闭
        } else {
            if (errno == EINTR) continue; // 信号中断，重试
            return -1; // 真正的错误
        }
    }
    return (ssize_t)received;
}

/* [FIX] 循环写，处理 partial write 和 EINTR，避免 IPC 流错位 */
static ssize_t write_exact(int fd, const void *buf, size_t len) {
    size_t sent = 0;
    const char *ptr = (const char *)buf;
    while (sent < len) {
        ssize_t ret = write(fd, ptr + sent, len - sent);
        if (ret > 0) {
            sent += ret;
        } else if (ret == 0) {
            return -1;
        } else {
            if (errno == EINTR) continue;
            return -1;
        }
    }
    return (ssize_t)sent;
}

static int g_qemu_clients[8];
static int g_client_count = 0;
static pthread_mutex_t g_client_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_push_barrier_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_push_barrier_cond = PTHREAD_COND_INITIALIZER;
static uint64_t g_push_barrier_next = 1;
static uint64_t g_push_barrier_done = 0;

extern void* broadcast_worker_thread(void* arg);
extern void* autonomous_monitor_thread(void* arg);
int g_sync_batch_size = 64;
void handle_ipc_rpc_passthrough(int qemu_fd, void *data, uint32_t len) { (void)qemu_fd; (void)data; (void)len; }

void load_swarm_config(const char *filename) {
    char error[256] = {0};
    const struct wvm_resource_vm *vm;

    if (!g_resource_plan_ready) {
        if (wvm_resource_plan_load(filename, &g_resource_plan, error,
                                   sizeof(error)) != 0) {
            fprintf(stderr, "[Config] %s\n", error);
            exit(1);
        }
        g_resource_plan_ready = 1;
    }

    printf("[Config] DHT Ring Size: %u Virtual Nodes (from %u Physical).\n",
           g_resource_plan.total_vnodes, g_resource_plan.node_count);
    for (uint32_t i = 0; i < g_resource_plan.node_count; i++) {
        const struct wvm_resource_node *node = &g_resource_plan.nodes[i];

        for (uint32_t slot = 0; slot < node->dht_slots; slot++) {
            uint32_t vnode = node->vnode_start + slot;
            u_ops.set_gateway_ip(WVM_ENCODE_ID(g_my_vm_id, vnode),
                                 inet_addr(node->ip), htons(node->port));
        }
    }
    wvm_set_mem_mapping(0, g_resource_plan.total_vnodes);

    wvm_clear_cpu_mappings();
    wvm_clear_memory_mappings();
    vm = wvm_resource_plan_get_vm(&g_resource_plan, g_my_vm_id);
    if (g_resource_plan.vm_count > 0 && !vm) {
        fprintf(stderr, "[Config] VM %u has no resource reservation\n",
                (unsigned)g_my_vm_id);
        exit(1);
    }

    if (vm) {
        for (uint32_t vcpu = 0; vcpu < vm->vcpu_count; vcpu++) {
            wvm_set_cpu_mapping((int)vcpu, vm->vcpu_nodes[vcpu]);
        }
        for (uint32_t chunk = 0; chunk < vm->memory_chunk_count; chunk++) {
            wvm_set_memory_mapping((int)chunk, vm->memory_nodes[chunk]);
        }
        printf("[Config] VM %u placement: %u vCPUs, %u x 1GiB memory chunks (%s).\n",
               (unsigned)g_my_vm_id, vm->vcpu_count,
               vm->memory_chunk_count,
               vm->policy == WVM_RESOURCE_POLICY_COMPACT ? "compact" : "spread");
        return;
    }

    /*
     * No new VM request: retain the historic core-weighted table exactly so
     * existing deployments and old VM directives keep their former routing.
     */
    int current_vcpu = 0;
    for (uint32_t i = 0; i < g_resource_plan.node_count; i++) {
        const struct wvm_resource_node *node = &g_resource_plan.nodes[i];

        for (uint32_t core = 0;
             core < node->cpu_capacity &&
             current_vcpu < WVM_CPU_ROUTE_TABLE_SIZE;
             core++) {
            wvm_set_cpu_mapping(current_vcpu++, node->vnode_start);
        }
    }
    for (uint32_t cursor = 0;
         current_vcpu < WVM_CPU_ROUTE_TABLE_SIZE;
         cursor = (cursor + 1) % g_resource_plan.node_count) {
        wvm_set_cpu_mapping(current_vcpu++,
                            g_resource_plan.nodes[cursor].vnode_start);
    }
    printf("[Config] CPU Routing Table Initialized (legacy core-weighted mode).\n");
}

/* 
 * [物理意图] 充当 QEMU 与分布式总线之间的“协议转换器”。
 * [关键逻辑] 拦截 IPC 管道中的缺页与 CPU 任务，调用 Logic Core 判定权属，并决定是本地执行还是发起网络 RPC。
 * [后果] 实现了前后端解耦。它保证了前端 QEMU 不需要理解复杂的 DHT 逻辑，只需发出“我要这块内存”的原始指令。
 */
static void handle_ipc_fault(int qemu_fd, struct wvm_ipc_fault_req* req) {
    struct wvm_ipc_fault_ack ack = {0}; // 使用扩展后的 ACK 结构
    fprintf(stderr, "[IPC Fault] gpa=%#llx len=%u vcpu=%u\n",
            (unsigned long long)req->gpa,
            req->len, req->vcpu_id);

    ack.status = wvm_handle_page_fault_logic(req->gpa, ack.data, &ack.version);
    if (ack.status == 0 && g_shm_ptr &&
        g_shm_size >= 4096 && req->gpa <= g_shm_size - 4096) {
        memcpy((uint8_t*)g_shm_ptr + req->gpa, ack.data, 4096);
    }
    fprintf(stderr, "[IPC Fault Ack] gpa=%#llx status=%d ver=%#llx\n",
            (unsigned long long)req->gpa,
            ack.status,
            (unsigned long long)ack.version);

    write_exact(qemu_fd, &ack, sizeof(ack));
}

static void handle_ipc_cpu_run(int qemu_fd, struct wvm_ipc_cpu_run_req* req) {
    struct wvm_ipc_cpu_run_ack ack = {0};
    int rpc_ret = 0;
    { static int __ipc_run=0;
      if (__ipc_run < 10) {
          fprintf(stderr, "[IPC VCPU_RUN] vcpu=%u mode=%u slave_id=%u\n",
                  req->vcpu_index, req->mode_tcg, (unsigned)req->slave_id);
          __ipc_run++;
      }
    }
    if (!WVM_IS_VALID_TARGET(req->slave_id)) {
        req->slave_id = wvm_get_compute_slave_id(req->vcpu_index);
    }
    if (!WVM_IS_VALID_TARGET(req->slave_id)) {
        ack.status = -ENODEV;
    } else if (req->mode_tcg) {
        /*
         * TCG remote execution needs the request metadata too.  Sending only
         * ctx.tcg drops vcpu_index, so the slave TCG process cannot align its
         * guest-visible CPU/APIC identity with the vCPU being executed.
         */
        rpc_ret = wvm_rpc_call(MSG_VCPU_RUN, req,
            sizeof(*req),
            req->slave_id, &ack, sizeof(ack));
        if (rpc_ret < 0) ack.status = rpc_ret;
        ack.mode_tcg = req->mode_tcg;
    } else {
        static int __ipc_kvm_ctx = 0;
        if (__ipc_kvm_ctx < 20) {
            fprintf(stderr,
                    "[IPC KVM CTX] vcpu=%u rip=0x%llx rax=0x%llx rdx=0x%llx mp_valid=%u mp=%u lapic=%u vcpu_events=%u tsc=%u\n",
                    req->vcpu_index,
                    (unsigned long long)req->ctx.kvm.rip,
                    (unsigned long long)req->ctx.kvm.rax,
                    (unsigned long long)req->ctx.kvm.rdx,
                    req->ctx.kvm.mp_state_valid,
                    req->ctx.kvm.mp_state,
                    req->ctx.kvm.lapic_valid,
                    req->ctx.kvm.vcpu_events_valid,
                    req->ctx.kvm.tsc_valid);
            __ipc_kvm_ctx++;
        }
        /*
         * KVM needs the same envelope as TCG.  The slave selects its KVM
         * vCPU from vcpu_index; forwarding only ctx.kvm silently aliases
         * every compact-path request to the receiver worker.
         */
        rpc_ret = wvm_rpc_call(MSG_VCPU_RUN, req,
            sizeof(*req),
            req->slave_id, &ack, sizeof(ack));
        if (rpc_ret < 0) ack.status = rpc_ret;
    }
    {
        static int __ipc_run_ret = 0;
        if (__ipc_run_ret < 20) {
            fprintf(stderr,
                    "[IPC VCPU_RUN RET] vcpu=%u mode=%u target=%u rpc_ret=%d ack_status=%d ack_mode=%u\n",
                    req->vcpu_index, req->mode_tcg, (unsigned)req->slave_id,
                    rpc_ret, ack.status, ack.mode_tcg);
            __ipc_run_ret++;
        }
    }
    write_exact(qemu_fd, &ack, sizeof(ack));
}

#define WVM_COMMIT_SYNC_WINDOW     128
#define WVM_COMMIT_SYNC_TIMEOUT_US 5000000ULL
#define WVM_COMMIT_SYNC_RETRY_US   50000ULL

struct pending_commit_sync {
    int active;
    uint64_t rid;
    uint8_t ack_status;
    uint8_t *pkt;
    size_t pkt_len;
    uint32_t dir_node;
    uint64_t gpa;
    uint64_t start_us;
    uint64_t last_send_us;
};

struct pending_commit_queue {
    struct pending_commit_sync entries[WVM_COMMIT_SYNC_WINDOW];
    unsigned head;
    unsigned count;
};

static void release_pending_commit(struct pending_commit_sync *entry)
{
    if (!entry->active) {
        return;
    }
    if (entry->pkt) {
        u_ops.free_packet(entry->pkt);
    }
    if (entry->rid != (uint64_t)-1) {
        u_ops.free_req_id(entry->rid);
    }
    memset(entry, 0, sizeof(*entry));
}

static int send_pending_commit(struct pending_commit_sync *entry)
{
    int ret = u_ops.send_packet(entry->pkt, (int)entry->pkt_len, entry->dir_node);
    if (ret == 0) {
        entry->last_send_us = u_ops.get_time_us();
    }
    return ret;
}

static int wait_oldest_pending_commit(struct pending_commit_queue *queue,
                                      uint64_t *fail_gpa)
{
    if (queue->count == 0) {
        return 0;
    }

    struct pending_commit_sync *entry = &queue->entries[queue->head];
    uint64_t last_wait_log_us = entry->start_us;
    while (u_ops.time_diff_us(entry->start_us) < WVM_COMMIT_SYNC_TIMEOUT_US) {
        if (u_ops.check_req_status(entry->rid) == 1) {
            int ret = entry->ack_status == 1 ? 0 : -EIO;
            static int ack_log_count;
            if (ret == 0 && ack_log_count < 20) {
                fprintf(stderr,
                        "[IPC COMMIT_SYNC] ack gpa=%#llx dir=%u rid=%llu\n",
                        (unsigned long long)entry->gpa,
                        (unsigned)entry->dir_node,
                        (unsigned long long)entry->rid);
                ack_log_count++;
            }
            if (ret < 0) {
                fprintf(stderr,
                        "[IPC COMMIT_SYNC] nack gpa=%#llx dir=%u rid=%llu\n",
                        (unsigned long long)entry->gpa,
                        (unsigned)entry->dir_node,
                        (unsigned long long)entry->rid);
                if (fail_gpa) {
                    *fail_gpa = entry->gpa;
                }
            }
            release_pending_commit(entry);
            queue->head = (queue->head + 1) % WVM_COMMIT_SYNC_WINDOW;
            queue->count--;
            return ret;
        }

        if (u_ops.time_diff_us(last_wait_log_us) > 1000000ULL) {
            static int slow_wait_log_count;
            if (slow_wait_log_count < 20) {
                fprintf(stderr,
                        "[IPC COMMIT_SYNC] waiting gpa=%#llx dir=%u rid=%llu elapsed_us=%llu\n",
                        (unsigned long long)entry->gpa,
                        (unsigned)entry->dir_node,
                        (unsigned long long)entry->rid,
                        (unsigned long long)u_ops.time_diff_us(entry->start_us));
                slow_wait_log_count++;
            }
            last_wait_log_us = u_ops.get_time_us();
        }

        if (u_ops.time_diff_us(entry->last_send_us) > WVM_COMMIT_SYNC_RETRY_US) {
            send_pending_commit(entry);
        }
        u_ops.yield_cpu_short_time();
    }

    fprintf(stderr, "[IPC COMMIT_SYNC] timeout gpa=%#llx dir=%u rid=%llu\n",
            (unsigned long long)entry->gpa,
            (unsigned)entry->dir_node,
            (unsigned long long)entry->rid);
    if (fail_gpa) {
        *fail_gpa = entry->gpa;
    }
    release_pending_commit(entry);
    queue->head = (queue->head + 1) % WVM_COMMIT_SYNC_WINDOW;
    queue->count--;
    return -ETIMEDOUT;
}

static int drain_pending_commits(struct pending_commit_queue *queue,
                                 uint64_t *fail_gpa)
{
    while (queue->count > 0) {
        int ret = wait_oldest_pending_commit(queue, fail_gpa);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

static void cancel_pending_commits(struct pending_commit_queue *queue)
{
    while (queue->count > 0) {
        struct pending_commit_sync *entry = &queue->entries[queue->head];
        release_pending_commit(entry);
        queue->head = (queue->head + 1) % WVM_COMMIT_SYNC_WINDOW;
        queue->count--;
    }
}

static int enqueue_commit_diff_sync(struct pending_commit_queue *queue,
                                    struct wvm_diff_log *log, uint32_t len,
                                    uint32_t dir_node)
{
    if (queue->count >= WVM_COMMIT_SYNC_WINDOW) {
        static int full_log_count;
        if (full_log_count < 20) {
            fprintf(stderr,
                    "[IPC COMMIT_SYNC] window full, draining oldest count=%u\n",
                    queue->count);
            full_log_count++;
        }
        int ret = wait_oldest_pending_commit(queue, NULL);
        if (ret < 0) {
            return ret;
        }
    }

    unsigned idx = (queue->head + queue->count) % WVM_COMMIT_SYNC_WINDOW;
    struct pending_commit_sync *entry = &queue->entries[idx];
    memset(entry, 0, sizeof(*entry));
    entry->rid = (uint64_t)-1;
    entry->dir_node = dir_node;
    entry->gpa = WVM_NTOHLL(log->gpa);
    entry->start_us = u_ops.get_time_us();

    entry->rid = u_ops.alloc_req_id(&entry->ack_status, sizeof(entry->ack_status));
    if (entry->rid == (uint64_t)-1) {
        return -EBUSY;
    }

    entry->pkt_len = sizeof(struct wvm_header) + len;
    entry->pkt = u_ops.alloc_packet(entry->pkt_len, 0);
    if (!entry->pkt) {
        u_ops.free_req_id(entry->rid);
        entry->rid = (uint64_t)-1;
        return -ENOMEM;
    }

    struct wvm_header *hdr = (struct wvm_header *)entry->pkt;
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = htonl(WVM_MAGIC);
    hdr->msg_type = htons(MSG_COMMIT_DIFF);
    hdr->payload_len = htons((uint16_t)len);
    hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
    hdr->target_id = htonl(dir_node);
    hdr->req_id = WVM_HTONLL(entry->rid);
    hdr->qos_level = 1;
    hdr->flags = WVM_FLAG_NEED_ACK;
    hdr->epoch = htonl(g_curr_epoch);
    hdr->node_state = g_my_node_state;
    memcpy(entry->pkt + sizeof(*hdr), log, len);

    entry->active = 1;
    if (send_pending_commit(entry) < 0) {
        release_pending_commit(entry);
        return -EIO;
    }

    queue->count++;
    return 0;
}

/* 
 * [物理意图] 维护 Wavelet 协议的“最后一百米”：将网络推送推入 QEMU 的监听线程。
 * [关键逻辑] 构造伪造的 wvm_header 封装入 IPC 包，强制唤醒 QEMU 的信号处理逻辑以更新本地 TLB/EPT。
 * [后果] 实现了“真理下达”。若此函数丢失，Daemon 虽然收到了数据，但 QEMU 里的 vCPU 依然会因为读到过期旧数据而崩溃。
 */
static void mark_push_barrier_done(uint64_t cookie)
{
    pthread_mutex_lock(&g_push_barrier_lock);
    if (cookie > g_push_barrier_done) {
        g_push_barrier_done = cookie;
    }
    static int push_barrier_ack_log_count;
    if (push_barrier_ack_log_count < 20) {
        fprintf(stderr, "[PUSH-BARRIER-ACK] cookie=%llu done=%llu\n",
                (unsigned long long)cookie,
                (unsigned long long)g_push_barrier_done);
        push_barrier_ack_log_count++;
    }
    pthread_cond_broadcast(&g_push_barrier_cond);
    pthread_mutex_unlock(&g_push_barrier_lock);
}

static int broadcast_push_to_qemu_locked(uint16_t msg_type, void* payload, int len)
{
    wvm_ipc_header_t ipc_hdr;
    int sent = 0;

    if (g_client_count <= 0) {
        return 0;
    }

    ipc_hdr.type = WVM_IPC_TYPE_INVALIDATE;
    ipc_hdr.len = sizeof(struct wvm_header) + len;

    uint8_t *buffer = malloc(sizeof(ipc_hdr) + ipc_hdr.len);
    if (!buffer) {
        return 0;
    }

    memcpy(buffer, &ipc_hdr, sizeof(ipc_hdr));
    struct wvm_header *hdr = (struct wvm_header *)(buffer + sizeof(ipc_hdr));
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = htonl(WVM_MAGIC);
    hdr->msg_type = htons(msg_type);
    hdr->payload_len = htons((uint16_t)len);
    hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
    hdr->target_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
    hdr->qos_level = (msg_type == MSG_PAGE_PUSH_FULL || msg_type == MSG_FORCE_SYNC) ? 0 : 1;
    hdr->epoch = htonl(g_curr_epoch);
    hdr->node_state = g_my_node_state;
    memcpy((void *)hdr + sizeof(*hdr), payload, len);

    for (int i = 0; i < g_client_count; i++) {
        if (write_exact(g_qemu_clients[i], buffer,
                        sizeof(ipc_hdr) + ipc_hdr.len) >= 0) {
            sent++;
        }
    }
    free(buffer);
    return sent;
}

static int send_push_barrier_locked(uint64_t *cookie_out)
{
    wvm_ipc_header_t ipc_hdr = {
        .type = WVM_IPC_TYPE_PUSH_BARRIER,
        .len = sizeof(uint64_t),
    };
    uint64_t cookie;

    if (g_client_count <= 0) {
        return 0;
    }

    pthread_mutex_lock(&g_push_barrier_lock);
    cookie = g_push_barrier_next++;
    pthread_mutex_unlock(&g_push_barrier_lock);

    /*
     * Caller must hold g_client_lock so no other PAGE_PUSH can be inserted
     * between this commit's push and the fence marker.
     */
    if (write_exact(g_qemu_clients[0], &ipc_hdr, sizeof(ipc_hdr)) < 0 ||
        write_exact(g_qemu_clients[0], &cookie, sizeof(cookie)) < 0) {
        return -EIO;
    }

    static int push_barrier_send_log_count;
    if (push_barrier_send_log_count < 20) {
        fprintf(stderr, "[PUSH-BARRIER] send cookie=%llu fd=%d clients=%d\n",
                (unsigned long long)cookie, g_qemu_clients[0], g_client_count);
        push_barrier_send_log_count++;
    }

    if (cookie_out) {
        *cookie_out = cookie;
    }
    return 0;
}

static int wait_for_push_barrier_cookie(uint64_t cookie, uint64_t timeout_us)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_us / 1000000ULL;
    ts.tv_nsec += (long)((timeout_us % 1000000ULL) * 1000ULL);
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&g_push_barrier_lock);
    while (g_push_barrier_done < cookie) {
        int rc = pthread_cond_timedwait(&g_push_barrier_cond,
                                        &g_push_barrier_lock, &ts);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&g_push_barrier_lock);
            return -ETIMEDOUT;
        }
    }
    pthread_mutex_unlock(&g_push_barrier_lock);
    return 0;
}

int wait_local_qemu_push_barrier(uint64_t timeout_us)
{
    pthread_mutex_lock(&g_client_lock);
    if (g_client_count <= 0) {
        pthread_mutex_unlock(&g_client_lock);
        return 0;
    }

    uint64_t cookie = 0;
    int ret = send_push_barrier_locked(&cookie);
    pthread_mutex_unlock(&g_client_lock);
    if (ret < 0) {
        return ret;
    }

    return wait_for_push_barrier_cookie(cookie, timeout_us);
}

int broadcast_push_to_qemu_fenced(uint16_t msg_type, void* payload, int len,
                                  uint64_t timeout_us)
{
    uint64_t cookie = 0;
    int ret;
    int sent;

    pthread_mutex_lock(&g_client_lock);
    sent = broadcast_push_to_qemu_locked(msg_type, payload, len);
    if (sent <= 0) {
        pthread_mutex_unlock(&g_client_lock);
        return sent;
    }

    ret = send_push_barrier_locked(&cookie);
    pthread_mutex_unlock(&g_client_lock);
    if (ret < 0) {
        return ret;
    }

    ret = wait_for_push_barrier_cookie(cookie, timeout_us);
    if (ret < 0) {
        return ret;
    }
    return sent;
}

int broadcast_push_to_qemu(uint16_t msg_type, void* payload, int len) {
    pthread_mutex_lock(&g_client_lock);
    int sent = broadcast_push_to_qemu_locked(msg_type, payload, len);
    pthread_mutex_unlock(&g_client_lock);
    return sent;
}

void broadcast_raw_packet_to_qemu(const void *packet, size_t len) {
    wvm_ipc_header_t ipc_hdr;
    ipc_hdr.type = WVM_IPC_TYPE_INVALIDATE;
    ipc_hdr.len = (uint32_t)len;

    pthread_mutex_lock(&g_client_lock);
    for (int i = 0; i < g_client_count; i++) {
        if (write_exact(g_qemu_clients[i], &ipc_hdr, sizeof(ipc_hdr)) < 0 ||
            write_exact(g_qemu_clients[i], packet, len) < 0) {
            fprintf(stderr, "[IPC] raw packet forward failed fd=%d errno=%d\n",
                    g_qemu_clients[i], errno);
        }
    }
    pthread_mutex_unlock(&g_client_lock);
}

void broadcast_irq_to_qemu(void) {
    wvm_ipc_header_t ipc_hdr;
    ipc_hdr.type = WVM_IPC_TYPE_IRQ;
    ipc_hdr.len = 0;
    
    pthread_mutex_lock(&g_client_lock);
    for (int i = 0; i < g_client_count; i++) {
        write_exact(g_qemu_clients[i], &ipc_hdr, sizeof(ipc_hdr));
    }
    pthread_mutex_unlock(&g_client_lock);
}

/* 
 * [物理意图] 维护 QEMU 前端与 Backend 守护进程之间的“生命脐带”。
 * [关键逻辑] 处理本地 IPC 请求，将 vCPU 的 COMMIT_DIFF 任务异步分发至分布式总线。
 * [后果] 这是本地算力与全局总线的交汇点。若此处的循环发生阻塞，vCPU 将产生明显的物理卡顿。
 */
void* client_handler(void *socket_desc) {
    int qemu_fd = *(int*)socket_desc;
    free(socket_desc);
    fprintf(stderr, "[IPC] client connected fd=%d\n", qemu_fd);

    int is_push_client = 0;

    wvm_ipc_header_t ipc_hdr;
    uint8_t payload_buf[WVM_MAX_PACKET_SIZE];
    struct pending_commit_queue commit_queue = {0};

    while (1) {
        // [FIX-G2] 使用 read_exact 处理 partial read
        ssize_t hdr_n = read_exact(qemu_fd, &ipc_hdr, sizeof(ipc_hdr));
        if (hdr_n < 0) {
            fprintf(stderr, "[IPC] header read failed fd=%d errno=%d\n",
                    qemu_fd, errno);
            break;
        }
        if (ipc_hdr.len > sizeof(payload_buf)) {
            fprintf(stderr, "[IPC] payload too large fd=%d type=%u len=%u max=%zu\n",
                    qemu_fd, (unsigned)ipc_hdr.type, (unsigned)ipc_hdr.len, sizeof(payload_buf));
            // Payload too large, drain and ignore
            char drain[1024];
            size_t remaining = ipc_hdr.len;
            while(remaining > 0) {
                ssize_t n = read(qemu_fd, drain, (remaining > sizeof(drain)) ? sizeof(drain) : remaining);
                if (n <= 0) break;
                remaining -= n;
            }
            continue;
        }
        
        // [FIX-G2] 使用 read_exact 处理 partial read
        ssize_t payload_n = read_exact(qemu_fd, payload_buf, ipc_hdr.len);
        if (payload_n < 0) {
            fprintf(stderr, "[IPC] payload read failed fd=%d type=%u need=%u errno=%d\n",
                    qemu_fd, (unsigned)ipc_hdr.type, (unsigned)ipc_hdr.len, errno);
            break;
        }

        switch (ipc_hdr.type) {
            case WVM_IPC_TYPE_REGISTER: {
                if (ipc_hdr.len != sizeof(uint32_t)) {
                    fprintf(stderr, "[IPC] invalid registration fd=%d len=%u\n",
                            qemu_fd, (unsigned)ipc_hdr.len);
                    break;
                }

                uint32_t role;
                memcpy(&role, payload_buf, sizeof(role));
                if (role == WVM_IPC_ROLE_ASYNC_PUSH) {
                    pthread_mutex_lock(&g_client_lock);
                    if (!is_push_client && g_client_count < (int)(sizeof(g_qemu_clients) /
                                                                   sizeof(g_qemu_clients[0]))) {
                        g_qemu_clients[g_client_count++] = qemu_fd;
                        is_push_client = 1;
                        fprintf(stderr,
                                "[IPC] fd=%d registered as async push client count=%d\n",
                                qemu_fd, g_client_count);
                    } else if (!is_push_client) {
                        fprintf(stderr, "[IPC] WARN: async push slots full fd=%d\n", qemu_fd);
                    }
                    pthread_mutex_unlock(&g_client_lock);
                } else {
                    fprintf(stderr, "[IPC] fd=%d registered as sync role=%u\n",
                            qemu_fd, (unsigned)role);
                }
                break;
            }
            case WVM_IPC_TYPE_PUSH_BARRIER_ACK: {
                if (ipc_hdr.len == sizeof(uint64_t)) {
                    uint64_t cookie;
                    memcpy(&cookie, payload_buf, sizeof(cookie));
                    mark_push_barrier_done(cookie);
                }
                break;
            }
            case WVM_IPC_TYPE_MEM_FAULT:
                handle_ipc_fault(qemu_fd, (struct wvm_ipc_fault_req*)payload_buf);
                break;
            case WVM_IPC_TYPE_CPU_RUN: {
                struct wvm_ipc_cpu_run_req *req =
                    (struct wvm_ipc_cpu_run_req*)payload_buf;
                static int cpu_run_rx_log_count;
                if (cpu_run_rx_log_count < 20) {
                    fprintf(stderr,
                            "[IPC CPU_RUN RX] fd=%d vcpu=%u mode=%u pending=%u\n",
                            qemu_fd, req->vcpu_index, req->mode_tcg,
                            commit_queue.count);
                }

                uint64_t fail_gpa = 0;
                int ret = drain_pending_commits(&commit_queue, &fail_gpa);
                if (ret < 0) {
                    struct wvm_ipc_cpu_run_ack ack = {0};
                    ack.status = ret;
                    ack.mode_tcg = req->mode_tcg;
                    ack.error_gpa = fail_gpa;
                    write_exact(qemu_fd, &ack, sizeof(ack));
                    break;
                }
                if (cpu_run_rx_log_count < 20) {
                    fprintf(stderr,
                            "[IPC CPU_RUN RX] fd=%d drain done vcpu=%u pending=%u\n",
                            qemu_fd, req->vcpu_index, commit_queue.count);
                    cpu_run_rx_log_count++;
                }
                handle_ipc_cpu_run(qemu_fd, req);
                break;
            }
            case WVM_IPC_TYPE_COMMIT_DIFF:
            case WVM_IPC_TYPE_COMMIT_DIFF_SYNC: {
                // This is the new IPC type for V29
                struct wvm_diff_log* log = (struct wvm_diff_log*)payload_buf;
                uint32_t dir_node = wvm_get_directory_node_id(WVM_NTOHLL(log->gpa));
                if (WVM_GET_NODEID(dir_node) == (uint32_t)g_my_node_id) {
                    /*
                     * Local directory commits must be applied before later IPC
                     * messages on the same QEMU connection, especially TCG
                     * CPU_RUN handoff.  Queueing through async send lets the
                     * remote vCPU pull a just-written page table before the
                     * directory copy is updated.
                     */
                    struct wvm_header hdr = {0};
                    hdr.magic = htonl(WVM_MAGIC);
                    hdr.msg_type = htons(MSG_COMMIT_DIFF);
                    hdr.payload_len = htons((uint16_t)ipc_hdr.len);
                    hdr.slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
                    hdr.target_id = htonl(dir_node);
                    hdr.epoch = htonl(g_curr_epoch);
                    hdr.node_state = g_my_node_state;
                    wvm_logic_process_packet(&hdr, log,
                                             WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
                } else if (ipc_hdr.type == WVM_IPC_TYPE_COMMIT_DIFF_SYNC) {
                    int ret = enqueue_commit_diff_sync(&commit_queue, log, ipc_hdr.len,
                                                       dir_node);
                    if (ret < 0) {
                        fprintf(stderr,
                                "[IPC COMMIT_SYNC] failed gpa=%#llx dir=%u ret=%d\n",
                                (unsigned long long)WVM_NTOHLL(log->gpa),
                                (unsigned)dir_node, ret);
                    }
                } else {
                    // Send MSG_COMMIT_DIFF to the correct directory node
                    u_ops.send_packet_async(MSG_COMMIT_DIFF, log, ipc_hdr.len, dir_node, 1);
                }
                break;
            }
            case WVM_IPC_TYPE_RPC_PASSTHROUGH: { // Type 99
                extern void handle_ipc_rpc_passthrough(int qemu_fd, void *data, uint32_t len);
                handle_ipc_rpc_passthrough(qemu_fd, payload_buf, ipc_hdr.len);
                break;
            }
            case WVM_IPC_TYPE_BLOCK_IO: {
                // 结构体必须与 QEMU 端严格对齐 (Packed 13 Bytes)
                struct wvm_ipc_block_req {
                    uint64_t lba;
                    uint32_t len;
                    uint8_t  is_write;
                    uint8_t  data[0];
                } __attribute__((packed));
                struct wvm_ipc_block_req *req = (void*)payload_buf;
                uint32_t target = wvm_get_storage_node_id(req->lba);
                
                size_t blk_size = sizeof(struct wvm_block_payload) + (req->is_write ? req->len : 0);
                size_t pkt_len = sizeof(struct wvm_header) + blk_size;
                
                // [FIX] 1. 分配 RX Buffer 接收远端真实数据
                size_t rx_buf_size = sizeof(struct wvm_block_payload) + req->len;
                uint8_t *rx_buf = malloc(rx_buf_size);
                uint64_t rid = u_ops.alloc_req_id(rx_buf, (uint32_t)rx_buf_size);
                
                uint8_t *pkt = u_ops.alloc_packet(pkt_len, 0);
                if (pkt && rid != (uint64_t)-1) {
                    struct wvm_header *h = (struct wvm_header *)pkt;
                    h->magic = htonl(WVM_MAGIC);
                    h->msg_type = htons(req->is_write ? MSG_BLOCK_WRITE : MSG_BLOCK_READ);
                    h->payload_len = htons(blk_size);
                    h->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
                    h->req_id = WVM_HTONLL(rid); // [FIX] 必须赋予请求ID才能收到ACK
                    h->qos_level = 1; 
                    
                    struct wvm_block_payload *p = (void*)(pkt + sizeof(*h));
                    p->lba = WVM_HTONLL(req->lba);
                    p->count = htonl(req->len / 512);
                    if (req->is_write) memcpy(p->data, req->data, req->len);
                    
                    h->crc32 = 0;
                    h->crc32 = htonl(calculate_crc32(pkt, pkt_len));
                    
                    // 2. 发送请求
                    u_ops.send_packet(pkt, pkt_len, target);
                    
                    // [FIX] 3. 阻塞等待远端存储节点回包
                    uint64_t t_start = u_ops.get_time_us();
                    int success = 0;
                    while (u_ops.time_diff_us(t_start) < 5000000) { // 5秒超时
                        if (u_ops.check_req_status(rid) == 1) {
                            // --- 完美闭环：检查硬件级坏道/写入错误 ---
                            struct wvm_header *rx_hdr = (struct wvm_header *)rx_buf;
                            if (rx_hdr->flags & WVM_FLAG_ERROR) {
                                fprintf(stderr, "[Storage] Remote Slave reported physical IO error on LBA!\n");
                                success = 0; // 物理落盘失败，向 QEMU 报告错误
                            } else {
                                success = 1; // 真正意义上的安全落盘
                            }
                            break;
                        }
                        usleep(100);
                    }
                    
                    // [FIX] 4. 向 QEMU 发送 ACK 唤醒 vCPU
                    uint8_t ack_byte = success ? 1 : 0;
                    write_exact(qemu_fd, &ack_byte, 1);
                    
                    // 如果是读操作，把远端拿回来的数据塞回给 QEMU
                    if (success && !req->is_write) {
                        struct wvm_block_payload *rx_p = (struct wvm_block_payload *)rx_buf;
                        write_exact(qemu_fd, rx_p->data, req->len);
                    }
                } else {
                    // 内存不足，直接回复失败，防止 QEMU 死锁
                    uint8_t ack_byte = 0;
                    write_exact(qemu_fd, &ack_byte, 1);
                }
                
                if (pkt) u_ops.free_packet(pkt);
                if (rid != (uint64_t)-1) u_ops.free_req_id(rid);
                free(rx_buf);
                break;
            }
            default:
                fprintf(stderr, "[IPC] unknown type fd=%d type=%u len=%u\n",
                        qemu_fd, (unsigned)ipc_hdr.type, (unsigned)ipc_hdr.len);
                break;
        }
    }
    if (drain_pending_commits(&commit_queue, NULL) < 0) {
        cancel_pending_commits(&commit_queue);
    }
    fprintf(stderr, "[IPC] client disconnected fd=%d\n", qemu_fd);
    close(qemu_fd);
    
    // 移除客户端并压缩数组，防止 Slot 耗尽
    if (is_push_client) {
        pthread_mutex_lock(&g_client_lock);
        for (int i = 0; i < g_client_count; i++) {
            if (g_qemu_clients[i] == qemu_fd) {
                // 将最后一个元素移到当前空位（无序数组删除法，效率 O(1)）
                if (i != g_client_count - 1) {
                    g_qemu_clients[i] = g_qemu_clients[g_client_count - 1];
                }
                g_client_count--;
                break;
            }
        }
        pthread_mutex_unlock(&g_client_lock);
    }
    
    return NULL;
}

/* 
 * [物理意图] 在无中心网络中注入“初始火种（Bootstrap Seeds）”。
 * [关键逻辑] 从配置中提取非本机的节点 IP，将其状态设为 SHADOW 并挂载到局部视图中，触发初始的 VIEW_PULL 请求。
 * [后果] 这是 P2P 网络的启动原点。若无此函数，节点将陷入“孤岛效应”，无法通过 Gossip 发现任何邻居。
 */
void load_initial_seeds(const char *config_file) {
    (void)config_file;

    if (!g_resource_plan_ready) {
        return;
    }

    for (uint32_t i = 0; i < g_resource_plan.node_count; i++) {
        const struct wvm_resource_node *node = &g_resource_plan.nodes[i];
        struct sockaddr_in seed = {0};

        seed.sin_family = AF_INET;
        seed.sin_addr.s_addr = inet_addr(node->ip);
        seed.sin_port = htons(node->port);
        for (uint32_t slot = 0; slot < node->dht_slots; slot++) {
            uint32_t vnode = node->vnode_start + slot;

            if (vnode == (uint32_t)g_my_node_id) {
                continue;
            }
            update_local_topology_view(vnode, 0, NODE_STATE_SHADOW, &seed, 0);
        }
    }
}

// --- Main Entry ---
int main(int argc, char **argv) {
    // Prevent process-wide termination on EPIPE when a peer disconnects.
    signal(SIGPIPE, SIG_IGN);

    // 参数检查
    if (argc < 7) {
        fprintf(stderr, "Usage: %s <RAM_MB> <LOCAL_PORT> <SWARM_CONFIG> <MY_PHYS_ID> <CTRL_PORT> <SLAVE_PORT> [SYNC_BATCH] [VM_ID]\n", argv[0]);
        return 1;
    }

    g_dev_fd = open("/dev/wavevm", O_RDWR);
    if (g_dev_fd < 0) {
        // 如果是纯用户态模式，这可能不是致命的，但在 Mode A 下是致命的。
        // 打印警告即可，方便调试
        perror("[Warning] Failed to open /dev/wavevm (Kernel Mode disabled?)");
    }


    // 1. 基础参数解析
    size_t ram_mb = (size_t)atol(argv[1]);
    g_shm_size = ram_mb * 1024 * 1024;
    int local_port = atoi(argv[2]);
    const char *config_file = argv[3];
    int my_phys_id = atoi(argv[4]); // 用户传入的是物理 ID (配置文件行号)
    g_ctrl_port = atoi(argv[5]);
    extern int g_slave_forward_port; 
    g_slave_forward_port = atoi(argv[6]);
    // 可选参数：批量同步大小
    if (argc >= 8) {
        extern int g_sync_batch_size;
        g_sync_batch_size = atoi(argv[7]);
    }
    // 可选参数：VM ID (Multi-VM 资源池化)
    if (argc >= 9) {
        g_my_vm_id = (uint8_t)atoi(argv[8]);
    }

    printf("[*] WaveVM Swarm V30.0 'Wavelet' Node Daemon (PhysID: %d, VM: %u)\n", my_phys_id, (unsigned)g_my_vm_id);

    /*
     * Resolve identity before starting backend RX threads.  Physical IDs are
     * placement keys; packet routing and local-sidecar selection use the
     * primary DHT vnode.
     */
    {
        char error[256] = {0};
        const struct wvm_resource_node *my_node;

        if (wvm_resource_plan_load(config_file, &g_resource_plan, error,
                                   sizeof(error)) != 0) {
            fprintf(stderr, "[Config] %s\n", error);
            return 1;
        }
        g_resource_plan_ready = 1;
        my_node = wvm_resource_plan_find_node(&g_resource_plan, my_phys_id);
        if (!my_node) {
            fprintf(stderr, "[Fatal] My Physical ID %d not found in config file!\n",
                    my_phys_id);
            return 1;
        }
        if (user_backend_init((int)my_node->vnode_start, local_port) != 0) {
            fprintf(stderr, "[-] Failed to init user backend.\n");
            return 1;
        }
    }

    // 2. Initialize user backend after topology identity has been resolved.
    if (!g_resource_plan_ready) {
        fprintf(stderr, "[-] Failed to init user backend.\n");
        return 1;
    }
    
    // 3. 初始化逻辑核心 (Logic Core)
    // 此时 Total Nodes 尚未知，传 0 作为提示
    if (wvm_core_init(&u_ops, 0) != 0) {
        fprintf(stderr, "[-] Logic Core init failed.\n");
        return 1;
    }
    
    // 4. 加载 Swarm 拓扑
    // 这会将所有物理 IP 展开为虚拟节点，并注入 Backend 和 Logic Core
    load_swarm_config(config_file);

    // 5. 启动 V29.5 核心推送引擎的多线程广播线程
    printf("[+] Starting %d Wavelet Broadcast Engines...\n", NUM_BCAST_WORKERS);
    for (long i = 0; i < NUM_BCAST_WORKERS; i++) { // 使用 long 避免指针转换警告
        pthread_t bcast_tid;
        // 将线程ID (0 to 7)作为参数传入
        if (pthread_create(&bcast_tid, NULL, broadcast_worker_thread, (void*)i) != 0) {
            perror("[-] Failed to start broadcast worker thread");
            exit(1);
        }
        pthread_detach(bcast_tid);
    }
    printf("[+] All Wavelet Broadcast Engines started.\n");

    // 6. Resolve physical identity and validate the local VM reservation.
    const struct wvm_resource_node *my_node =
        g_resource_plan_ready
            ? wvm_resource_plan_find_node(&g_resource_plan, my_phys_id)
            : NULL;
    const struct wvm_resource_vm *my_vm =
        g_resource_plan_ready
            ? wvm_resource_plan_get_vm(&g_resource_plan, g_my_vm_id)
            : NULL;
    int my_virtual_id = my_node ? (int)my_node->vnode_start : -1;
    uint32_t my_local_cores = my_node ? my_node->cpu_capacity : 1;
    uint64_t required_memory_mb = my_node ? my_node->memory_mb : 0;

    if (my_vm) {
        my_local_cores =
            wvm_resource_plan_local_vcpus(&g_resource_plan, g_my_vm_id,
                                           my_phys_id);
        required_memory_mb =
            wvm_resource_plan_local_memory_mb(&g_resource_plan, g_my_vm_id,
                                              my_phys_id);
    }

    if (my_virtual_id == -1) {
        fprintf(stderr, "[Fatal] My Physical ID %d not found in config file!\n", my_phys_id);
        return 1;
    }
    if (g_shm_size < required_memory_mb * 1024U * 1024U) {
        fprintf(stderr, "\n[FATAL] Resource Mismatch!\n");
        fprintf(stderr, "  Config VM/node reservation requires: %llu MB\n",
                (unsigned long long)required_memory_mb);
        fprintf(stderr, "  Launch arg provided:                 %lu MB\n",
                ram_mb);
        return 1;
    }
    printf("[Check] Resource verified: Alloc %lu MB >= Reservation %llu MB.\n",
           ram_mb, (unsigned long long)required_memory_mb);

    // 7. 将真实的虚拟 ID 注入 Logic Core
    // Logic Core 将根据此 ID 判断是否拥有某个 GPA 的管理权 (Directory Owner)
    wvm_set_my_node_id(my_virtual_id);
    printf("[Init] Identity Mapped: PhysID %d -> VirtualID %d (Primary)\n", my_phys_id, my_virtual_id);
    // Mode A owns a separate Logic Core instance inside wavevm.ko.
    inject_vm_id(g_my_vm_id);
    inject_mem_global(0, g_resource_plan.total_vnodes);
    inject_mem_global(1, (uint32_t)my_virtual_id);
    inject_cpu_route_table();
    inject_memory_route_table();
    {
        char split_buf[32];
        snprintf(split_buf, sizeof(split_buf), "%u", my_local_cores);
        setenv("WVM_LOCAL_SPLIT", split_buf, 1);
    }

    // 8. 初始化共享内存 (RAM Backing Store)
    // 优先读取环境变量，支持单机多实例测试
    const char *shm_path = getenv("WVM_SHM_FILE");
    if (!shm_path) shm_path = WVM_DEFAULT_SHM_PATH; // "/wavevm_ram"

    printf("[System] Initializing SHM: %s (Size: %lu MB)\n", shm_path, ram_mb);

    // 清理残留
    shm_unlink(shm_path);
    
    int shm_fd = shm_open(shm_path, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) { 
        fprintf(stderr, "[-] Failed to open shm file '%s': %s\n", shm_path, strerror(errno));
        return 1; 
    }
    
    // 分配物理空间
    if (ftruncate(shm_fd, g_shm_size) < 0) {
        perror("ftruncate failed");
        close(shm_fd);
        return 1;
    }

    // 映射到进程空间
    g_shm_ptr = mmap(NULL, g_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd); // 映射后即可关闭 fd
    
    if (g_shm_ptr == MAP_FAILED) { 
        perror("mmap failed"); 
        return 1; 
    }
    
    // 可选：预热内存 (避免运行时缺页抖动)
    // memset(g_shm_ptr, 0, g_shm_size);
    printf("[+] Memory Ready at %p\n", g_shm_ptr);

    // 9. 启动 UNIX Socket 监听 (QEMU 接口)
    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket AF_UNIX failed");
        return 1;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    
    // 动态生成 Socket 路径，支持多实例
    char *inst_id = getenv("WVM_INSTANCE_ID");
    char sock_path[128];
    snprintf(sock_path, sizeof(sock_path), "/tmp/wvm_user_%s.sock", inst_id ? inst_id : "0");

    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    unlink(sock_path); // 绑定前确保文件不存在

    printf("[System] Control Socket: %s\n", sock_path);

    // 关键：设置环境变量供子进程 (QEMU) 使用
    setenv("WVM_ENV_SOCK_PATH", sock_path, 1);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { 
        perror("bind unix socket failed"); 
        return 1; 
    }
    
    if (listen(listen_fd, 100) < 0) {
        perror("listen failed");
        return 1;
    }

    printf("[+] WaveVM V29 Node Ready. Waiting for QEMU...\n");

    // 10. Backend/Logic Core 已在前面初始化并注入拓扑。
    // 此处严禁重复初始化，否则会重置 CPU 路由表为 AUTO_ROUTE。

    // 11. [自治扩展] 加载种子节点，不要求全量配置
    load_initial_seeds(config_file);

    // 12. 启动自治监控引擎 (Part 3 中定义的线程)
    pthread_t monitor_tid;
    pthread_create(&monitor_tid, NULL, autonomous_monitor_thread, NULL);
    pthread_detach(monitor_tid);

    // 13. [Bootstrap] 视图主动拉取逻辑暂时禁用（需跨模块可见 peer 结构体）。

    printf("[Autonomous] Node started in SHADOW mode. Auto-scaling into cluster...\n");

    // 14. 主循环：接受 QEMU 连接
    while (1) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept error");
            // 生产环境可能选择 sleep 并重试，而非退出
            sleep(1);
            continue;
        }

        // [FIX-F1] 防御性检查：在 accept 后立即检查连接数上限，防止线程爆炸。
        // 旧代码无条件 pthread_create，仅在 client_handler 内部检查 MAX_QEMU_CLIENTS，
        // 但线程已经创建完毕。此处前置检查，超限直接拒绝连接。
        pthread_mutex_lock(&g_client_lock);
        int current_count = g_client_count;
        pthread_mutex_unlock(&g_client_lock);

        if (current_count >= MAX_QEMU_CLIENTS) {
            fprintf(stderr, "[IPC] WARN: MAX_QEMU_CLIENTS(%d) reached, rejecting fd=%d\n",
                    MAX_QEMU_CLIENTS, client_fd);
            close(client_fd);
            continue;
        }

        // 为每个 QEMU 连接创建一个处理线程
        pthread_t thread_id;
        int *new_sock = malloc(sizeof(int));
        if (new_sock) {
            *new_sock = client_fd;
            if (pthread_create(&thread_id, NULL, client_handler, (void*)new_sock) != 0) {
                perror("pthread_create failed");
                close(client_fd);
                free(new_sock);
            } else {
                pthread_detach(thread_id);
            }
        } else {
            perror("malloc failed");
            close(client_fd);
        }
    }

    return 0;
}
