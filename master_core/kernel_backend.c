/*
 * [IDENTITY] Kernel Backend - The Bare-Metal Engine
 * ---------------------------------------------------------------------------
 * 物理角色：宿主机内核中的"内存直通总线"。
 * 职责边界：
 * 1. 实现异步缺页等待队列，通过网络中断直接唤醒 vCPU。
 * 2. 实现内核态脏页捕获 (page_mkwrite)，在软中断中完成 Diff 计算。
 * 3. 管理 Radix-Tree 索引，实现对远程 PUSH 的零拷贝应用。
 * 
 * [禁止事项]
 * - 严禁在 sigsegv 路径上使用阻塞型互斥锁 (Mutex)，必须使用 spinlock_irqsave。
 * - 严禁绕过 unmap_mapping_range 直接修改 PTE。
 * ---------------------------------------------------------------------------
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/udp.h>
#include <linux/ip.h>
#include <linux/socket.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/nmi.h>      
#include <linux/delay.h>    
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/atomic.h>
#include <asm/barrier.h>
#include <linux/spinlock.h>
#include <linux/percpu.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,0)
#include <linux/unaligned.h>
#else
#include <asm/unaligned.h>
#endif
#include <linux/kthread.h>
#include <linux/wait.h>
#include <asm/byteorder.h>
#include <linux/radix-tree.h>
#include <linux/highmem.h>
#include <linux/random.h>
#include <linux/pagemap.h> 
#include <linux/rmap.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>

#include "../common_include/wavevm_ioctl.h"
#include "../common_include/wavevm_protocol.h"
#include "unified_driver.h"
#include "logic_core.h" 

#define DRIVER_NAME "wavevm"
#define TX_RING_SIZE 2048
#define TX_SLOT_SIZE 65535

// [Fix] Multi-VM: 引用全局 vm_id，用于 composite ID 编码
// In kernel module context, define it here (main_wrapper.c is userspace-only)
uint8_t g_my_vm_id = 0;
EXPORT_SYMBOL(g_my_vm_id);

// 定义并初始化 mapping 锁
static struct rw_semaphore g_mapping_sem; 

static int service_port = 9000;
module_param(service_port, int, 0644);
static int local_slave_port = 9001; 
module_param(local_slave_port, int, 0644);

static struct socket *g_socket = NULL;
static struct sockaddr_in gateway_table[WVM_MAX_GATEWAYS]; 
static struct kmem_cache *wvm_pkt_cache = NULL;

// --- [V29 Wavelet] 内核态脏区捕获与Diff提交结构 ---

// 记录一个正在被写入的页面
struct diff_task_t {
    struct list_head list;
    struct page *page;      // 目标物理页
    void *pre_image;        // 4KB 快照 (修改前的数据)
    uint64_t gpa;           // Guest Physical Address
    uint64_t timestamp;     // 记录时间，用于防抖
};

static LIST_HEAD(g_diff_queue);
static spinlock_t g_diff_lock;
static struct task_struct *g_committer_thread = NULL;
static wait_queue_head_t g_diff_wq;

// 引用：我们需要知道 VMA 的 mapping 才能触发 unmap
static struct address_space *g_mapping = NULL;

// --- ID 管理系统 ---
#define BITS_PER_CPU_ID 16
#define MAX_IDS_PER_CPU (1 << BITS_PER_CPU_ID) 
#define CPU_ID_SHIFT    16                     

struct id_pool_t {
    uint32_t *ids;
    uint32_t head;
    uint32_t tail;
    spinlock_t lock;
};
static DEFINE_PER_CPU(struct id_pool_t, g_id_pool);

// 请求上下文：增加等待队列以支持异步休眠
struct req_ctx_t {
    void *rx_buffer;
    uint32_t generation;
    uint32_t max_len; // [FIX-G3] 记录 rx_buffer 最大可用长度
    volatile int done;
    wait_queue_head_t wq; // [New] 内核任务在此休眠
    struct task_struct *waiter; // [New] 记录等待的任务 (用于调试或唤醒检查)
};
static struct req_ctx_t *g_req_ctx = NULL;
// Runtime-sized: nr_cpu_ids * MAX_IDS_PER_CPU. The original fixed sizing
// (1024 * 65536) is too large for most test environments and can stall insmod.
static size_t g_req_ctx_count = 0;

// [V29 Final Fix] 内核态页面元数据
// 用于在 Radix Tree 中同时存储物理页指针和版本号
typedef struct {
    struct page *page;
    uint64_t version;
    struct rcu_head rcu;
    
    // 内核原生 sk_buff 队列，自带 spinlock，用于缓存乱序到达的包
    struct sk_buff_head reorder_q; 
} kvm_page_meta_t;

// 修改树的定义，名字保持不变，但存储的内容变了
static RADIX_TREE(g_page_tree, GFP_ATOMIC);
static DEFINE_SPINLOCK(g_page_tree_lock);

// --- QoS 发送队列 ---
struct tx_slot_t {
    int len;
    uint32_t target_id;
    uint8_t data[TX_SLOT_SIZE];
};

struct wvm_tx_ring_t {
    struct tx_slot_t *slots;
    uint32_t head; 
    uint32_t tail; 
    spinlock_t lock;
    atomic_t pending_count;
};

static struct wvm_tx_ring_t g_fast_ring;
static struct wvm_tx_ring_t g_slow_ring;

static struct task_struct *g_tx_thread = NULL;
static wait_queue_head_t g_tx_wq;

static wait_queue_head_t g_irq_wait_queue;
static atomic_t g_irq_pending = ATOMIC_INIT(0);

// 定义工作项结构，用于把参数带到 Workqueue 里
struct wvm_inval_work {
    struct work_struct work;
    uint64_t gpa;
};

static void init_ring(struct wvm_tx_ring_t *ring) {
    ring->slots = vmalloc(sizeof(struct tx_slot_t) * TX_RING_SIZE);
    ring->head = ring->tail = 0;
    spin_lock_init(&ring->lock);
    atomic_set(&ring->pending_count, 0);
}

// --- 工作线程回调（运行在进程上下文，安全！）---
static void wvm_inval_work_fn(struct work_struct *work) {
    struct wvm_inval_work *iw = container_of(work, struct wvm_inval_work, work);
    
    down_read(&g_mapping_sem);
    if (g_mapping) {
        loff_t offset = (loff_t)iw->gpa;
        unmap_mapping_range(g_mapping, offset, PAGE_SIZE, 1);
    }
    up_read(&g_mapping_sem);
    
    kfree(iw); // 任务完成，释放工作项本身
}

// --- 调度函数（可以在原子上下文中调用）---
static void schedule_async_unmap(uint64_t gpa) {
    // 必须使用 GFP_ATOMIC，因为我们在软中断里
    struct wvm_inval_work *iw = kmalloc(sizeof(*iw), GFP_ATOMIC);
    if (iw) {
        INIT_WORK(&iw->work, wvm_inval_work_fn);
        iw->gpa = gpa;
        schedule_work(&iw->work); // 扔给系统默认队列，内核会择机执行
    } else {
        // 极罕见的 OOM，打印错误但只能放弃 Unmap
        // 后果仅仅是 Guest 短时间内读到旧数据，不会崩系统
        printk(KERN_ERR "[WVM] OOM scheduling unmap for GPA %llx\n", gpa);
    }
}

// --- 辅助函数 ---
static uint64_t k_get_time_us(void) { return ktime_to_us(ktime_get()); }
static void k_touch_watchdog(void) { touch_nmi_watchdog(); }
static int k_is_atomic_context(void) { return in_atomic() || irqs_disabled(); }
static void k_cpu_relax(void) { cpu_relax(); }
static uint64_t k_time_diff_us(uint64_t start) {
    uint64_t now = k_get_time_us();
    return (now >= start) ? (now - start) : ((uint64_t)(-1) - start + now);
}

static void WVM_PRINTF_LIKE(1, 2) k_log(const char *fmt, ...) {
    struct va_format vaf;
    va_list args;
    va_start(args, fmt);
    vaf.fmt = fmt;
    vaf.va = &args;
    printk(KERN_INFO "[WVM]: %pV\n", &vaf);
    va_end(args);
}

static void k_get_random(uint32_t *val) { get_random_bytes(val, sizeof(uint32_t)); }
static void k_yield_short(void) { if (!in_atomic()) msleep(1); else udelay(50); }

// --- 内存操作 ---
static void* k_alloc_large_table(size_t size) { return vzalloc(size); }
static void k_free_large_table(void *ptr) { vfree(ptr); }
static void* k_alloc_packet(size_t size, int atomic) {
    gfp_t flags = atomic ? GFP_ATOMIC : GFP_KERNEL;
    if (wvm_pkt_cache && size <= (sizeof(struct wvm_header) + 4096)) {
        return kmem_cache_alloc(wvm_pkt_cache, flags);
    }
    return kmalloc(size, flags);
}
static void k_free_packet(void *ptr) {
    if (!ptr) return;
    /*
     * Kernel >= 6.x no longer exposes slab cache pointer in struct page.
     * kfree() can free both kmalloc() and slab objects, and is sufficient here.
     */
    kfree(ptr);
}
static void k_fetch_page(uint64_t gpa, void *buf) {} // Deprecated by logic_core

/* 
 * [物理意图] 从内核索引中“抹除”特定 GPA 的元数据存在。
 * [关键逻辑] 在原子上下文中将页面从 Radix-Tree 中摘除，并使用 kfree_rcu 安全回收乱序缓冲区。
 * [后果] 这是 MESI 协议在内核态落地的第一步，它切断了后续 PUSH 包对该物理页的自动更新链路。
 */
static void k_invalidate_meta_atomic(uint64_t gpa) {
    spin_lock(&g_page_tree_lock);
    kvm_page_meta_t *meta = radix_tree_delete(&g_page_tree, gpa >> PAGE_SHIFT);
    spin_unlock(&g_page_tree_lock);
    
    if (meta) {
        // 必须释放队列中所有积压的 skb，否则内存泄漏
        skb_queue_purge(&meta->reorder_q);
        
        // 等待所有 RCU 读者读完后释放结构体
        kfree_rcu(meta, rcu);
    }
}

/* 
 * [物理意图] 实现“零锁竞争”的分布式请求 ID 分配。
 * [关键逻辑] 采用 Per-CPU 变量技术，每个逻辑核拥有独立的 ID 池。利用 Generation 机制检测网络乱序导致的 ABA 冲突。
 * [后果] 这一步是 Mode A 支持百万 TPS 的基石。它消除了传统分布式系统中全局 ID 产生器的串行化瓶颈。
 */
static uint64_t k_alloc_req_id(void *rx_buffer, uint32_t buffer_size) {
    uint64_t id = (uint64_t)-1;
    unsigned long flags;
    int cpu = get_cpu();
    struct id_pool_t *pool = this_cpu_ptr(&g_id_pool);

    spin_lock_irqsave(&pool->lock, flags);
    if (unlikely(!pool->ids)) goto out;
    if (pool->tail != pool->head) {
        uint32_t raw_idx = pool->ids[pool->head & (MAX_IDS_PER_CPU - 1)];
        pool->head++;
        uint32_t combined_idx = ((uint32_t)cpu << CPU_ID_SHIFT) | raw_idx;
        if (likely((size_t)combined_idx < g_req_ctx_count)) {
            g_req_ctx[combined_idx].generation++;
            if (g_req_ctx[combined_idx].generation == 0) g_req_ctx[combined_idx].generation = 1;
            id = ((uint64_t)g_req_ctx[combined_idx].generation << 32) | combined_idx;
            WRITE_ONCE(g_req_ctx[combined_idx].rx_buffer, rx_buffer);
            g_req_ctx[combined_idx].max_len = buffer_size; // [FIX-G3]
            WRITE_ONCE(g_req_ctx[combined_idx].done, 0);
            smp_wmb(); 
        } else { pool->head--; }
    }
out:
    spin_unlock_irqrestore(&pool->lock, flags);
    put_cpu();
    return id;
}

static void k_free_req_id(uint64_t full_id) {
    unsigned long flags;
    uint32_t generation = (uint32_t)(full_id >> 32);
    uint32_t combined_idx = (uint32_t)(full_id & 0xFFFFFFFF);
    int owner_cpu = (combined_idx >> CPU_ID_SHIFT);
    uint32_t raw_idx = combined_idx & (MAX_IDS_PER_CPU - 1);
    struct id_pool_t *pool;

    if (unlikely((size_t)combined_idx >= g_req_ctx_count || owner_cpu >= nr_cpu_ids)) return;
    if (g_req_ctx[combined_idx].generation != generation) return; 

    xchg(&g_req_ctx[combined_idx].rx_buffer, NULL);
    WRITE_ONCE(g_req_ctx[combined_idx].done, 0);

    pool = per_cpu_ptr(&g_id_pool, owner_cpu);
    if (unlikely(!pool->ids)) return;
    spin_lock_irqsave(&pool->lock, flags);
    pool->ids[pool->tail & (MAX_IDS_PER_CPU - 1)] = raw_idx;
    pool->tail++;
    spin_unlock_irqrestore(&pool->lock, flags);
}

static int k_check_req_status(uint64_t full_id) {
    uint32_t combined_idx = (uint32_t)(full_id & 0xFFFFFFFF);
    if ((size_t)combined_idx >= g_req_ctx_count) return -1;
    if (READ_ONCE(g_req_ctx[combined_idx].done)) { smp_rmb(); return 1; }
    return 0;
}

// --- 发送逻辑 ---
static void k_set_gateway_ip(uint32_t gw_id, uint32_t ip, uint16_t port) {
    // [Fix #1] 入参可能是 composite ID，先解码为裸 node_id 再索引静态数组
    uint32_t raw_node_id = WVM_GET_NODEID(gw_id);
    if (raw_node_id < WVM_MAX_GATEWAYS) {
        gateway_table[raw_node_id].sin_family = AF_INET;
        gateway_table[raw_node_id].sin_addr.s_addr = ip;
        gateway_table[raw_node_id].sin_port = port;
    }
}

static int raw_kernel_send(void *data, int len, uint32_t target_id) {
    struct msghdr msg; struct kvec vec; struct sockaddr_in to_addr; int ret;
    // [Fix #1] target_id 可能是 composite ID，解码为裸 node_id 索引 gateway_table
    uint32_t raw_id = WVM_GET_NODEID(target_id);
    if (!g_socket || raw_id >= WVM_MAX_GATEWAYS || gateway_table[raw_id].sin_port == 0) return -ENODEV;

    to_addr = gateway_table[raw_id];
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &to_addr;
    msg.msg_namelen = sizeof(to_addr);
    msg.msg_flags = MSG_DONTWAIT;
    vec.iov_base = data;
    vec.iov_len = len;

    ret = kernel_sendmsg(g_socket, &msg, &vec, 1, len);
    if (ret == -EAGAIN) {
        if (in_atomic() || irqs_disabled()) udelay(5);
    }
    return ret;
}

/* 
 * [物理意图] 模拟处理器的“输出流水线”，负责将内核态产生的 Diff 和 ACK 投射到网络。
 * [关键逻辑] 严格执行优先级调度：Fast Ring（信令）绝对抢占 Slow Ring（全页）。同时将 CRC32 计算卸载到工作线程中。
 * [后果] 保证了系统在“内存洪流”中依然能及时响应心跳与版本确认包，防止了由于网络拥塞导致的误判定节点下线。
 */
static int tx_worker_thread_fn(void *data) {
    /* Large TX slots (up to 64KB) cannot live on kernel stack. */
    struct tx_slot_t *slot = kvzalloc(sizeof(*slot), GFP_KERNEL);
    if (!slot) return -ENOMEM;
    while (!kthread_should_stop()) {
        wait_event_interruptible(g_tx_wq, 
            atomic_read(&g_fast_ring.pending_count) > 0 || 
            atomic_read(&g_slow_ring.pending_count) > 0 || kthread_should_stop());
        
        if (kthread_should_stop()) break;

        // 1. Fast Lane (Priority)
        while (atomic_read(&g_fast_ring.pending_count) > 0) {
            int found = 0;
            spin_lock_bh(&g_fast_ring.lock);
            if (g_fast_ring.head != g_fast_ring.tail) {
                memcpy(slot, &g_fast_ring.slots[g_fast_ring.head], sizeof(*slot));
                g_fast_ring.head = (g_fast_ring.head + 1) % TX_RING_SIZE;
                atomic_dec(&g_fast_ring.pending_count);
                found = 1;
            }
            spin_unlock_bh(&g_fast_ring.lock);
            
            if (found) { 
                // [V29] CRC Calculation offloaded to worker thread
                struct wvm_header *hdr = (struct wvm_header *)slot->data;
                hdr->crc32 = 0;
                hdr->crc32 = htonl(calculate_crc32(slot->data, slot->len));
                
                raw_kernel_send(slot->data, slot->len, slot->target_id); 
                cond_resched(); 
            }
        }

        // 2. Slow Lane (Quota)
        int quota = 32; 
        while (quota-- > 0 && atomic_read(&g_slow_ring.pending_count) > 0) {
            int found = 0;
            spin_lock_bh(&g_slow_ring.lock);
            if (g_slow_ring.head != g_slow_ring.tail) {
                memcpy(slot, &g_slow_ring.slots[g_slow_ring.head], sizeof(*slot));
                g_slow_ring.head = (g_slow_ring.head + 1) % TX_RING_SIZE;
                atomic_dec(&g_slow_ring.pending_count);
                found = 1;
            }
            spin_unlock_bh(&g_slow_ring.lock);
            
            if (found) {
                // [V29] CRC Calculation
                struct wvm_header *hdr = (struct wvm_header *)slot->data;
                hdr->crc32 = 0;
                hdr->crc32 = htonl(calculate_crc32(slot->data, slot->len));
                
                raw_kernel_send(slot->data, slot->len, slot->target_id);
            }
            // Preempt if fast packet arrives
            if (atomic_read(&g_fast_ring.pending_count) > 0) break;
        }
        k_touch_watchdog();
    }
    kvfree(slot);
    return 0;
}

// [PATCH] 全局 Epoch 变量
static uint32_t g_kernel_epoch = 0;

// [修改] k_send_packet 函数
static int k_send_packet(void *data, int len, uint32_t target_id) {
    struct wvm_header *hdr = (struct wvm_header *)data;
    
    // [PATCH] 注入当前 Epoch
    hdr->epoch = htonl(g_kernel_epoch);
    // Node State 在内核态通常默认为 ACTIVE (2)，因为能跑内核模块说明节点活着
    hdr->node_state = 2; 
    hdr->target_id = htonl(target_id);

    hdr->crc32 = 0;
    hdr->crc32 = htonl(calculate_crc32(data, len));
    
    if (!k_is_atomic_context()) return raw_kernel_send(data, len, target_id);

    struct wvm_tx_ring_t *ring = (hdr->qos_level == 1) ? &g_fast_ring : &g_slow_ring;
    unsigned long flags;
    spin_lock_irqsave(&ring->lock, flags);
    uint32_t next = (ring->tail + 1) % TX_RING_SIZE;
    if (next != ring->head) {
        ring->slots[ring->tail].len = len;
        ring->slots[ring->tail].target_id = target_id;
        memcpy(ring->slots[ring->tail].data, data, len);
        ring->tail = next;
        atomic_inc(&ring->pending_count);
        wake_up_interruptible(&g_tx_wq);
    }
    spin_unlock_irqrestore(&ring->lock, flags);
    return 0;
}

static void k_send_packet_async(uint16_t msg_type, void* payload, int len, uint32_t target_id, uint8_t qos) {
    size_t pkt_len = sizeof(struct wvm_header) + len;
    uint8_t *buffer = k_alloc_packet(pkt_len, 1);
    if (!buffer) return;

    struct wvm_header *hdr = (struct wvm_header *)buffer;
    extern int g_my_node_id;
    hdr->magic = htonl(WVM_MAGIC);
    hdr->msg_type = htons(msg_type);
    hdr->payload_len = htons(len);
    hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
    hdr->req_id = 0;
    hdr->qos_level = qos;
    if (payload && len > 0) memcpy(buffer + sizeof(*hdr), payload, len);

    k_send_packet(buffer, pkt_len, target_id);
    k_free_packet(buffer);
}

// 引用外部函数
extern int wvm_handle_local_fault_fastpath(uint64_t gpa, void* page_buffer, uint64_t *version);
extern void wvm_declare_interest_in_neighborhood(uint64_t gpa);

#define MAX_WVM_SLOTS 32

struct wvm_mem_slot {
    uint64_t start_gpa;
    uint64_t size;
    uint64_t host_offset; // 对应后端 SHM 文件的偏移
    bool active;
};

static struct wvm_mem_slot g_mem_slots[MAX_WVM_SLOTS];

// [NEW IOCTL] 动态注入 Guest 内存布局
static int wvm_set_mem_layout(struct wvm_ioctl_mem_layout *layout) {
    if (layout->count > MAX_WVM_SLOTS) return -EINVAL;

    /* Reset old slots first to avoid stale ranges after restart/reconfigure. */
    for (int i = 0; i < MAX_WVM_SLOTS; i++) {
        g_mem_slots[i].active = false;
        g_mem_slots[i].start_gpa = 0;
        g_mem_slots[i].size = 0;
        g_mem_slots[i].host_offset = 0;
    }

    for (int i = 0; i < layout->count; i++) {
        g_mem_slots[i].start_gpa = layout->slots[i].start;
        g_mem_slots[i].size = layout->slots[i].size;
        g_mem_slots[i].active = true;
        printk(KERN_INFO "[WVM] Active RAM Slot %d: GPA 0x%llx - 0x%llx\n", 
               i, g_mem_slots[i].start_gpa, g_mem_slots[i].start_gpa + g_mem_slots[i].size);
    }
    return 0;
}

/* 
 * [物理意图] 接管处理器的“缺页异常（EPT Violation）”，实现虚拟化空间的物理坍缩。
 * [关键逻辑] 1. 本地命中路径：直接拷贝 Directory 内容；2. 远程路径：利用 wait_queue 将 vCPU 线程挂起，实现异步网络拉取。
 * [后果] 这是整个项目最高频的入口点。通过异步休眠替代了 V28 的自旋死等，使得物理 CPU 可以在等待内存期间去跑其他任务。
 */
static vm_fault_t wvm_fault_handler(struct vm_fault *vmf) {
    uint64_t gpa = (uint64_t)vmf->pgoff << PAGE_SHIFT;
    bool is_valid_ram = false;

    // 严谨的地址合法性检查
    for (int i = 0; i < MAX_WVM_SLOTS; i++) {
        if (g_mem_slots[i].active && 
            gpa >= g_mem_slots[i].start_gpa && 
            gpa < g_mem_slots[i].start_gpa + g_mem_slots[i].size) {
            is_valid_ram = true;
            break;
        }
    }

    if (!is_valid_ram) {
        // 如果 GPA 落在了 PCI Hole 或未映射区域，绝对不能继续
        return VM_FAULT_SIGBUS; 
    }

    uint32_t dir_node = wvm_get_directory_node_id(gpa);
    struct page *page;
    void *page_addr;
    kvm_page_meta_t *meta;
    int ret;

    // 1. 分配目标物理页 (HighMem 兼容)
    page = alloc_page(GFP_HIGHUSER_MOVABLE);
    if (unlikely(!page)) {
        return VM_FAULT_OOM;
    }

    // 2. 为 V29 版本控制分配元数据结构
    meta = kmalloc(sizeof(kvm_page_meta_t), GFP_KERNEL);
    if (unlikely(!meta)) {
        __free_page(page);
        return VM_FAULT_OOM;
    }
    meta->page = page;
    meta->version = 0;
    // 初始化乱序重排队列头
    skb_queue_head_init(&meta->reorder_q); 

    // ========================================================================
    // 分支 A: 本地缺页 (Local Fault) - 极速路径
    // ========================================================================
    if (WVM_GET_NODEID(dir_node) == (uint32_t)g_my_node_id) {
        uint64_t local_version = 0;
        
        // 建立临时内核映射进行拷贝 (原子上下文)
        page_addr = kmap_atomic(page);
        
        // 调用 Logic Core 直接获取数据
        ret = wvm_handle_local_fault_fastpath(gpa, page_addr, &local_version);
        
        kunmap_atomic(page_addr);

        if (ret == 0) {
            // 成功：更新元数据版本
            meta->version = local_version;

            // 插入 VMA
            if (vm_insert_page(vmf->vma, vmf->address, page) != 0) {
                kfree(meta);
                __free_page(page);
                return VM_FAULT_SIGBUS;
            }
            
            // 注册到全局索引树 (用于后续接收 PUSH)
            spin_lock(&g_page_tree_lock);
            radix_tree_insert(&g_page_tree, gpa >> PAGE_SHIFT, meta);
            spin_unlock(&g_page_tree_lock);

            // 释放 alloc_page 的引用，现在由 VMA 管理
            put_page(page);

            // [V29] 宣告兴趣
            wvm_declare_interest_in_neighborhood(gpa);
            
            return VM_FAULT_NOPAGE;
        } else {
            // 本地查找失败 (逻辑错误或严重不一致)
            kfree(meta);
            __free_page(page);
            return VM_FAULT_SIGBUS;
        }
    } 
    
    // ========================================================================
    // 分支 B: 远程缺页 (Remote Fault) - 无限重试版
    // ========================================================================
    else {
        uint64_t rid;
        long timeout_ret;
        
        // 1. 分配跳板缓冲区 (Bounce Buffer)
        size_t ack_payload_size = sizeof(struct wvm_mem_ack_payload);
        void *bounce_buf = kmalloc(ack_payload_size, GFP_KERNEL);
        if (unlikely(!bounce_buf)) {
            kfree(meta);
            __free_page(page);
            return VM_FAULT_OOM;
        }

        // 2. 分配请求 ID
        rid = k_alloc_req_id(bounce_buf, (uint32_t)ack_payload_size);
        if (rid == (uint64_t)-1) {
            kfree(bounce_buf);
            kfree(meta);
            __free_page(page);
            return VM_FAULT_SIGBUS;
        }

        // 3. 构造请求包
        uint64_t net_gpa = WVM_HTONLL(gpa);
        size_t pkt_len = sizeof(struct wvm_header) + sizeof(net_gpa);
        
        // --- 重试循环上下文 ---
        unsigned long timeout_jiffies = msecs_to_jiffies(10); // 初始 10ms
        unsigned long max_timeout = msecs_to_jiffies(2000);   // 最大 2s
        
        // 首次发送
        uint8_t *buffer = k_alloc_packet(pkt_len, 1);
        if (likely(buffer)) {
            struct wvm_header *hdr = (struct wvm_header *)buffer;
            hdr->magic = htonl(WVM_MAGIC);
            hdr->msg_type = htons(MSG_MEM_READ);
            hdr->payload_len = htons(sizeof(net_gpa));
            hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, dir_node));
            hdr->req_id = WVM_HTONLL(rid);
            hdr->qos_level = 1;
            hdr->crc32 = 0;
            memcpy(buffer + sizeof(*hdr), &net_gpa, sizeof(net_gpa));
            k_send_packet(buffer, pkt_len, dir_node);
            k_free_packet(buffer);
        }

        while (1) {
            // 4. 睡眠等待
            timeout_ret = wait_event_interruptible_timeout(
                g_req_ctx[(uint32_t)rid].wq, 
                READ_ONCE(g_req_ctx[(uint32_t)rid].done) == 1, 
                timeout_jiffies
            );

            // 5. 成功检查
            if (READ_ONCE(g_req_ctx[(uint32_t)rid].done) == 1) {
                break; // 数据到了，跳出
            }

            // 6. 信号中断检查 (防止死锁)
            if (signal_pending(current)) {
                k_log("Page fault interrupted by signal. GPA: %llx", gpa);
                k_free_req_id(rid);
                kfree(bounce_buf);
                kfree(meta);
                __free_page(page);
                return VM_FAULT_SIGBUS; 
            }

            // 7. 超时重发 (指数退避)
            if (timeout_ret == 0) {
                timeout_jiffies *= 2;
                if (timeout_jiffies > max_timeout) timeout_jiffies = max_timeout;
                
                buffer = k_alloc_packet(pkt_len, 1);
                if (buffer) {
                    struct wvm_header *hdr = (struct wvm_header *)buffer;
                    hdr->magic = htonl(WVM_MAGIC);
                    hdr->msg_type = htons(MSG_MEM_READ);
                    hdr->payload_len = htons(sizeof(net_gpa));
                    hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, dir_node));
                    hdr->req_id = WVM_HTONLL(rid);
                    hdr->qos_level = 1;
                    hdr->crc32 = 0;
                    memcpy(buffer + sizeof(*hdr), &net_gpa, sizeof(net_gpa));
                    k_send_packet(buffer, pkt_len, dir_node);
                    k_free_packet(buffer);
                }
            }
        }

        // --- 数据处理 ---
        k_free_req_id(rid); 

        // 从 Bounce Buffer 拷贝数据
        struct wvm_mem_ack_payload *ack = (struct wvm_mem_ack_payload *)bounce_buf;
        meta->version = WVM_NTOHLL(ack->version);
        
        page_addr = kmap_atomic(page);
        memcpy(page_addr, ack->data, 4096);
        kunmap_atomic(page_addr);
        
        kfree(bounce_buf);

        if (vm_insert_page(vmf->vma, vmf->address, page) != 0) {
            kfree(meta);
            __free_page(page);
            return VM_FAULT_SIGBUS;
        }
        
        spin_lock(&g_page_tree_lock);
        radix_tree_insert(&g_page_tree, gpa >> PAGE_SHIFT, meta);
        spin_unlock(&g_page_tree_lock);

        put_page(page);

        // V29: 宣告兴趣
        wvm_declare_interest_in_neighborhood(gpa);

        return VM_FAULT_NOPAGE;
    }
}

/* 
 * [物理意图] 内核态“内存小波”生成器，计算物理页的增量变动。
 * [关键逻辑] 在无锁状态下对比“前快照”与“现数据”，生成最小化的 Diff 日志，并通过 COMMIT_DIFF 消息提交给全网。
 * [后果] 实现了极低带宽下的高频内存同步。它只传输变动的比特，使得在千兆网环境下也能模拟出万兆总线的写入吞吐量。
 */
static int committer_thread_fn(void *data) {
    while (!kthread_should_stop()) {
        struct diff_task_t *task;
        
        // 1. 等待任务
        wait_event_interruptible(g_diff_wq, 
            !list_empty(&g_diff_queue) || kthread_should_stop());
            
        if (kthread_should_stop()) break;

        // 2. 取出一个任务
        spin_lock_bh(&g_diff_lock);
        if (list_empty(&g_diff_queue)) {
            spin_unlock_bh(&g_diff_lock);
            continue;
        }
        task = list_first_entry(&g_diff_queue, struct diff_task_t, list);
        list_del(&task->list);
        spin_unlock_bh(&g_diff_lock);

        // 3. 计算 Diff (Compare current page with pre-image)
        void *current_data = kmap_atomic(task->page);
        
        // --- 优化版 Diff 算法 ---
        int first_diff = -1, last_diff = -1;
        
        // 1. 使用 64位 (8字节) 指针进行快速扫描
        uint64_t *p64_curr = (uint64_t *)current_data;
        uint64_t *p64_pre  = (uint64_t *)task->pre_image;
        int num_words = 4096 / 8; // 512 次循环
        
        for (int i = 0; i < num_words; i++) {
            if (p64_curr[i] != p64_pre[i]) {
                // 发现不同！记录位置
                // 这里的 i 是 8字节的索引，转为字节索引
                int byte_offset = i * 8;
                
                if (first_diff == -1) {
                    first_diff = byte_offset; // 粗略定位到 8字节块
                }
                last_diff = byte_offset + 7;  // 粗略定位到 8字节块末尾
            }
        }
        
        kunmap_atomic(current_data);
        
        // 4. 发送 Diff
        if (first_diff != -1) {
            uint16_t offset = first_diff;
            uint16_t size = last_diff - first_diff + 1;
            
            // 构造 COMMIT_DIFF 包
            size_t payload_size = sizeof(struct wvm_diff_log) + size;
            size_t pkt_len = sizeof(struct wvm_header) + payload_size;
            
            uint8_t *buffer = k_alloc_packet(pkt_len, 0); // Can sleep here
            if (buffer) {
                struct wvm_header *hdr = (struct wvm_header *)buffer;
                hdr->magic = htonl(WVM_MAGIC);
                hdr->msg_type = htons(MSG_COMMIT_DIFF);
                hdr->payload_len = htons(payload_size);
                extern int g_my_node_id;
                hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
                hdr->req_id = 0;
                hdr->qos_level = 1;

                struct wvm_diff_log *log = (struct wvm_diff_log*)(buffer + sizeof(*hdr));
                log->gpa = WVM_HTONLL(task->gpa);
                rcu_read_lock();
                kvm_page_meta_t *meta = radix_tree_lookup(&g_page_tree, task->gpa >> PAGE_SHIFT);
                // 如果找到了元数据，就填入网络序的版本号；找不到(极罕见)则填0触发强制同步作为保底
                log->version = meta ? WVM_HTONLL(meta->version) : 0;
                rcu_read_unlock();
                log->offset = htons(offset);
                log->size = htons(size);
                
                // Copy diff data
                current_data = kmap_atomic(task->page);
                memcpy(log->data, (uint8_t*)current_data + offset, size);
                kunmap_atomic(current_data);
                
                // Send
                uint32_t dir_node = wvm_get_directory_node_id(task->gpa);
                k_send_packet(buffer, pkt_len, dir_node);
                k_free_packet(buffer);
            }
        }

        // 5. [Critical] 重新启用写保护 (Reset cycle)
        // 必须调用 unmap_mapping_range 来清除 PTE 的写权限
        // 这样下一次写入才会再次触发 page_mkwrite
        down_read(&g_mapping_sem); // 读锁，允许睡眠
        if (g_mapping) {
            unmap_mapping_range(g_mapping, (loff_t)task->gpa, PAGE_SIZE, 1);
        }
        up_read(&g_mapping_sem);

        // 6. 清理资源
        vfree(task->pre_image);
        put_page(task->page); // 释放我们在 mkwrite 中获取的引用
        kfree(task);
    }
    return 0;
}

/* 
 * [物理意图] 捕获 Guest 对“只读缓存页”的第一次写动作，启动 Wavelet 提交周期。
 * [关键逻辑] 当 Guest 试图修改内存时拦截请求，执行 Copy-Before-Write 快照，并将原始数据放入 Committer 队列。
 * [后果] 它是分布式写操作的触发器。通过此钩子，WaveVM 能在不修改 Guest 内核的情况下，精准感知每一个字节的变动。
 */
static vm_fault_t wvm_page_mkwrite(struct vm_fault *vmf) {
    struct page *page = vmf->page;
    uint64_t gpa = (uint64_t)vmf->pgoff << PAGE_SHIFT;
    
    // 1. 分配任务结构和快照内存
    struct diff_task_t *task = kmalloc(sizeof(*task), GFP_KERNEL);
    if (!task) return VM_FAULT_OOM;
    
    task->pre_image = vmalloc(4096);
    if (!task->pre_image) {
        kfree(task);
        return VM_FAULT_OOM;
    }

    // 2. 捕获快照 (Copy-Before-Write)
    // 此时页面还是旧数据
    void *vaddr = kmap_atomic(page);
    memcpy(task->pre_image, vaddr, 4096);
    kunmap_atomic(vaddr);

    task->page = page;
    get_page(page); // 增加引用计数，防止在线程处理前被释放
    task->gpa = gpa;
    task->timestamp = k_get_time_us();

    // 3. 加入队列
    // [FIX-M3] 统一使用 spin_lock_bh，与取出端保持一致，防止 softirq 死锁
    spin_lock_bh(&g_diff_lock);
    list_add_tail(&task->list, &g_diff_queue);
    spin_unlock_bh(&g_diff_lock);

    // 4. 唤醒提交线程
    wake_up_interruptible(&g_diff_wq);

    // 5. 允许写入
    // 返回 VM_FAULT_LOCKED 后，内核会将 PTE 设为可写
    return VM_FAULT_LOCKED;
}

// --- 接收处理 ---
extern void wvm_logic_process_packet(struct wvm_header *hdr, void *payload, uint32_t source_id);

// [V29 Core] 尝试应用队列中的乱序包 (链式反应)
// 当收到 v+1 包后，调用此函数检查是否缓存了 v+2, v+3...
static void try_flush_reorder_q(kvm_page_meta_t *meta) {
    struct sk_buff *skb, *tmp;
    bool progress = true;

    // 循环直到无法继续推进版本 (链条断裂)
    while (progress) {
        progress = false;
        
        // 必须加锁，防止软中断并发修改队列
        spin_lock(&meta->reorder_q.lock);
        
        skb_queue_walk_safe(&meta->reorder_q, skb, tmp) {
            // skb->data 存储的是完整的 wvm_header + payload
            struct wvm_header *hdr = (struct wvm_header *)skb->data;
            void *pl = skb->data + sizeof(struct wvm_header);
            
            // 解析包类型与版本号
            uint16_t type = ntohs(hdr->msg_type);
            uint64_t ver = 0;
            void *data_ptr = NULL;
            uint16_t offset = 0;
            uint32_t len = 0;

            if (type == MSG_PAGE_PUSH_DIFF) {
                struct wvm_diff_log *log = (struct wvm_diff_log *)pl;
                ver = WVM_NTOHLL(log->version);
                offset = ntohs(log->offset);
                len = ntohs(log->size);
                data_ptr = log->data;
                // 边界检查
                if (offset + len > 4096) len = 0; 
            } else if (type == MSG_PAGE_PUSH_FULL) {
                struct wvm_full_page_push *full = (struct wvm_full_page_push *)pl;
                ver = WVM_NTOHLL(full->version);
                data_ptr = full->data;
                len = 4096;
            }

            // 命中逻辑：这是我们紧缺的下一个版本
            if (is_next_version(meta->version, ver)) {
                // 1. 从队列摘除 (Unlink)
                __skb_unlink(skb, &meta->reorder_q);
                
                // 2. 应用数据
                // kmap_atomic 可以在自旋锁内使用 (不会睡眠)
                if (len > 0 && data_ptr) {
                    void *vaddr = kmap_atomic(meta->page);
                    memcpy((uint8_t*)vaddr + offset, data_ptr, len);
                    kunmap_atomic(vaddr);
                }
                
                // 3. 更新版本号并标记脏
                meta->version = ver;
                SetPageDirty(meta->page);
                
                // 4. 释放 skb 内存
                kfree_skb(skb);
                
                // 5. 标记进度，跳出遍历，重新开始 (因为链表结构已变)
                progress = true; 
                break; 
            }
            // 过期逻辑：版本号小于等于当前，直接丢弃
            else if (ver <= meta->version) {
                __skb_unlink(skb, &meta->reorder_q);
                kfree_skb(skb);
            }
            // 未来逻辑：ver > meta->version + 1，继续留在队列里等待
        }
        spin_unlock(&meta->reorder_q.lock);
    }
}

/* 
 * [物理意图] 实现“零拷贝”真理更新，将远程推送直接注入 Guest 的物理页。
 * [关键逻辑] 校验版本号：顺序包直接 kmap 写入，乱序包存入 skb_head 队列进行重排。处理完后触发 try_flush 链式应用。
 * [后果] 这是 V30 性能超越 V28 的秘密武器。它在内核底层完成了数据的“无感更新”，Guest 甚至不知道内存已经变了。
 */
static void handle_kernel_push(struct wvm_header *hdr, void *payload) {
    uint16_t type = ntohs(hdr->msg_type);
    uint8_t flags = hdr->flags; 
    uint64_t gpa, push_version;
    void *data_ptr;
    uint32_t data_len;
    uint16_t offset = 0;

    // --- 1. 解析 Payload ---
    if (type == MSG_PAGE_PUSH_FULL) {
        struct wvm_full_page_push *push = (struct wvm_full_page_push *)payload;
        gpa = WVM_NTOHLL(push->gpa);
        push_version = WVM_NTOHLL(push->version);
        data_ptr = push->data;
        data_len = 4096;
    } else if (type == MSG_PAGE_PUSH_DIFF) {
        struct wvm_diff_log *log = (struct wvm_diff_log *)payload;
        gpa = WVM_NTOHLL(log->gpa);
        push_version = WVM_NTOHLL(log->version);
        offset = ntohs(log->offset);
        data_len = ntohs(log->size);
        data_ptr = log->data;
        if (offset + data_len > 4096) return;
    } else {
        return;
    }

    // --- 2. 核心处理 ---
    rcu_read_lock(); 
    kvm_page_meta_t *meta = radix_tree_lookup(&g_page_tree, gpa >> PAGE_SHIFT);
    
    if (meta && meta->page) {
        // [FIX] 幂等性检查：如果包版本 <= 本地版本，静默丢弃
        // 内核态也必须防御重放攻击或网络乱序
        if (!is_newer_version(meta->version, push_version)) {
            rcu_read_unlock();
            return;
        }
        // [场景 A] 顺序到达 (Ideal Case)
        // 如果是全量推送 (Full Push)，或者是连续的 Diff，直接应用
        // 这解决了 Epoch 切换时版本号大跳跃导致的内核死锁问题
        if (is_next_version(meta->version, push_version)) {
            void *vaddr = kmap_atomic(meta->page); 
            if (flags & WVM_FLAG_ZERO) {
                memset(vaddr, 0, 4096);
            } else if (data_len > 0) {
                memcpy((uint8_t*)vaddr + offset, data_ptr, data_len);
            }
            kunmap_atomic(vaddr);
            
            meta->version = push_version;
            SetPageDirty(meta->page);
            try_flush_reorder_q(meta); // 应用后尝试消化队列里的包
        } 
        // [场景 B] 乱序到达 (Future Packet) - 存入队列
        else if (is_newer_version(meta->version, push_version) && !is_next_version(meta->version, push_version)) {
            // 限制队列深度为 8，防止内存耗尽攻击
            if (skb_queue_len(&meta->reorder_q) < 8) {
                // 我们必须分配一个新的 skb 来持久化保存数据
                // 因为当前函数的 payload 指针指向的是临时的网络栈 buffer
                size_t total_len = sizeof(struct wvm_header) + ntohs(hdr->payload_len);
                struct sk_buff *saved_skb = alloc_skb(total_len, GFP_ATOMIC);
                
                if (saved_skb) {
                    // 拷贝完整的 Header + Payload
                    skb_put_data(saved_skb, hdr, total_len); 
                    // 入队 (skb_queue_tail 是并发安全的，但为了和 try_flush 互斥，内部自带锁)
                    skb_queue_tail(&meta->reorder_q, saved_skb);
                }
            }
            // 如果队列满了，丢弃该包，只能等待发送端重传或 V28 兜底
        }
        // [场景 C] 过期包 - 忽略
    }
    rcu_read_unlock(); 
}

/* 
 * [物理意图] 内核态“先知”指令加速器，执行全网同步的物理内存填充。
 * [关键逻辑] 遍历 Radix-Tree 映射的物理页，利用 wmb 内存屏障和 memset 直接操作硬件页，并强制推进逻辑时钟。
 * [后果] 它让分布式虚拟机的“冷启动”与“克隆”速度达到了硬件极限，绕过了所有缓慢的软件协议栈处理路径。
 */
static void handle_kernel_rpc_batch(void *payload, uint32_t payload_len) {
    if (payload_len < sizeof(struct wvm_rpc_batch_memset)) return;

    struct wvm_rpc_batch_memset *batch = (struct wvm_rpc_batch_memset *)payload;
    uint32_t count = ntohl(batch->count);
    uint32_t val = ntohl(batch->val); // 通常是 0
    uint8_t fill_byte = (uint8_t)val;

    // 边界检查
    size_t required_len = sizeof(struct wvm_rpc_batch_memset) + count * sizeof(struct wvm_rpc_region);
    if (payload_len < required_len) return;

    struct wvm_rpc_region *regions = (struct wvm_rpc_region *)(batch + 1);

    // 遍历所有内存段
    for (uint32_t i = 0; i < count; i++) {
        uint64_t gpa_start = WVM_NTOHLL(regions[i].gpa);
        uint64_t len = WVM_NTOHLL(regions[i].len);
        uint64_t gpa_end = gpa_start + len;
        uint64_t cur_gpa = gpa_start;

        // 按页遍历 (Page Walk)
        while (cur_gpa < gpa_end) {
            // 计算当前页内的操作范围
            uint64_t page_base = cur_gpa & PAGE_MASK;
            uint64_t page_offset = cur_gpa & ~PAGE_MASK;
            uint64_t bytes_in_page = PAGE_SIZE - page_offset;
            uint64_t remaining = gpa_end - cur_gpa;
            uint64_t op_len = (remaining < bytes_in_page) ? remaining : bytes_in_page;

            rcu_read_lock();
            // 1. 在 Radix Tree 中查找物理页元数据
            kvm_page_meta_t *meta = radix_tree_lookup(&g_page_tree, page_base >> PAGE_SHIFT);
            
            if (meta && meta->page) {
                // 2. 建立临时原子映射 (HighMem 兼容)
                void *vaddr = kmap_atomic(meta->page);
                
                // 3. 执行物理内存填充
                memset((uint8_t*)vaddr + page_offset, fill_byte, op_len);
                // [FIX] DMA Fence: 确保数据刷入内存，对设备可见
                wmb(); 
                kunmap_atomic(vaddr);

                // 4. 标记脏页并更新版本号 (逻辑一致性)
                SetPageDirty(meta->page);
                meta->version = MAKE_VERSION(g_kernel_epoch, GET_COUNTER(meta->version) + 1);
                
                // 注意：这里不需要广播 Diff，因为 Prophet 的设计假设是
                // 发送端已经广播了 RPC 指令，所有节点都会执行这个操作。
                // 我们只需要更新本地状态即可。
            }
            // 如果 meta 不存在：说明该页尚未被分配/缺页。
            // 对于 memset 0 来说，新分配的页本身就是 0，所以跳过是安全的。
            // 对于 memset 非0，当缺页触发时，会拉取最新数据。
            
            rcu_read_unlock();

            cur_gpa += op_len;
        }
    }
}

/* 
 * [物理意图] 内核态“安全筛网”，负责流量的物理合法性判定与分流。
 * [关键逻辑] 1. 执行强制 CRC32 完整性校验；2. 识别消息类型并根据优先级分发给 Prophet 引擎或逻辑核心。
 * [后果] 这是内核驱动的防火墙。它确保了只有格式正确、且未被篡改的数据包能接触到 Guest 的物理内存。
 */
static void internal_process_single_packet(struct wvm_header *hdr, uint32_t src_ip) {
    // 1. [V29 新增] CRC32 强校验
    // 这是 V28 没有的。如果不加，V29 的高可靠性就是空谈。
    uint32_t pkt_len = sizeof(struct wvm_header) + ntohs(hdr->payload_len);
    uint32_t received_crc = ntohl(hdr->crc32);
    
    // 临时清零 CRC 字段进行计算
    hdr->crc32 = 0; 
    uint32_t calculated = calculate_crc32(hdr, pkt_len);
    
    if (calculated != received_crc) {
        // 校验失败，直接静默丢弃，防止污染状态
        // 生产环境可以加一行 printk 但注意频率
        return; 
    }
    
    // 2. 解析通用字段
    void *payload = (void *)hdr + sizeof(struct wvm_header);
    uint16_t type = ntohs(hdr->msg_type);
    
    // 3. [V30 核心修复] 识别 CPU/存储包，内核直接“踢”给本地 Slave 端口
    if (type == MSG_VCPU_RUN || type == MSG_BLOCK_WRITE || 
        type == MSG_BLOCK_READ || type == MSG_BLOCK_FLUSH) {
        
        struct sockaddr_in s_addr = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
            .sin_port = htons(local_slave_port)
        };
        struct msghdr msg = { .msg_name = &s_addr, .msg_namelen = sizeof(s_addr), .msg_flags = MSG_DONTWAIT };
        struct kvec vec = { .iov_base = hdr, .iov_len = pkt_len };
        kernel_sendmsg(g_socket, &msg, &vec, 1, pkt_len); // 内核直发 Loopback
        return;
    }

    // 4. 拦截物理中断包
    // Logic Core 无法处理此消息，必须在内核层直接唤醒 QEMU 等待队列
    if (type == MSG_VFIO_IRQ) {
        atomic_set(&g_irq_pending, 1);
        wake_up_interruptible(&g_irq_wait_queue);
        return; 
    }

    // 5. [V29 核心] Prophet 处理
    if (type == MSG_RPC_BATCH_MEMSET) {
        uint32_t payload_len = ntohs(hdr->payload_len);
        handle_kernel_rpc_batch(payload, payload_len);
        return; // 处理完毕直接返回，不需要 Logic Core 介入
    }


    // 6. [V29 核心] 拦截主动推送 (Direct Kernel Push)
    // 这是 V29 性能翻倍的关键。必须在内核态直接处理 Diff/Full Push。
    if (type == MSG_PAGE_PUSH_FULL || type == MSG_PAGE_PUSH_DIFF) {
        handle_kernel_push(hdr, payload);
        return;
    }

    // 7. [V28 继承] 处理 ACK (唤醒等待线程)
    if (type == MSG_MEM_ACK || type == MSG_VCPU_EXIT) {
        uint64_t rid = WVM_NTOHLL(hdr->req_id);
        uint32_t combined_idx = (uint32_t)(rid & 0xFFFFFFFF);
        if ((size_t)combined_idx < g_req_ctx_count) {
            struct req_ctx_t *ctx = &g_req_ctx[combined_idx];
            // 检查 Generation 防止 ABA
            if ((rid >> 32) == ctx->generation) {
                if (ctx->rx_buffer) {
                    // V29 的 Payload 包含 Version，结构体变了，但 memcpy 逻辑通用
                    // [FIX-G3] 截断保护：防止恶意/错乱包溢出 rx_buffer
                    size_t copy_len = ntohs(hdr->payload_len);
                    if (copy_len > ctx->max_len)
                        copy_len = ctx->max_len;
                    memcpy(ctx->rx_buffer, payload, copy_len);
                }
                WRITE_ONCE(ctx->done, 1);
                wake_up_interruptible(&ctx->wq); // V29 用 wait_queue 替代了 V28 的自旋
            }
        }
        return;
    }
    
    // 8. [V28 继承] 其他信令转交 Logic Core
    // V29 的 Logic Core 需要知道是谁发的 (Source ID)
    uint32_t src_id = ntohl(hdr->slave_id);
    wvm_logic_process_packet(hdr, payload, src_id);
}

/* 
 * [物理意图] 网卡驱动级别的“数据就绪”回调，执行高PPS解包循环。
 * [关键逻辑] 拦截 Socket 接收队列，对聚合的 UDP 数据包执行循环解构，提取每一个嵌入的 WVM 子包头。
 * [后果] 配合 Gateway 的聚合逻辑，它在内核态实现了“多包合一”的高效处理，极大地减轻了 CPU 软中断的负荷。
 */
static void wavevm_udp_data_ready(struct sock *sk) {
    struct sk_buff *skb;
    while ((skb = skb_dequeue(&sk->sk_receive_queue)) != NULL) {
        // 1. 线性化：确保 skb->data 是连续内存，方便指针操作
        if (skb_is_nonlinear(skb) && skb_linearize(skb) != 0) { 
            kfree_skb(skb); 
            continue; 
        }

        uint32_t offset = 0;
        uint32_t total_len = skb->len;
        struct iphdr *iph = ip_hdr(skb);
        uint32_t src_ip = iph->saddr;

        // 2. 内核态循环解包 (De-aggregation Loop)
        while (offset + sizeof(struct wvm_header) <= total_len) {
            struct wvm_header *hdr = (struct wvm_header *)(skb->data + offset);

            // 魔数校验：防止处理垃圾数据导致 panic
            if (ntohl(hdr->magic) != WVM_MAGIC) break;

            uint16_t p_len = ntohs(hdr->payload_len);
            uint32_t pkt_len = sizeof(struct wvm_header) + p_len;

            // 边界检查
            if (offset + pkt_len > total_len) break;

            // 调用单包处理逻辑 (注意：该函数内部已包含 CRC 校验)
            internal_process_single_packet(hdr, src_ip);

            // 3. 指针后移
            offset += pkt_len;
        }

        kfree_skb(skb);
    }
}

/* 
 * [物理意图] 物理级映射撤销：强行将 Guest 的硬件 PTE 变为无效。
 * [关键逻辑] 通过 Workqueue 异步触发 unmap_mapping_range，强制冲刷所有物理核心的 TLB 缓存。
 * [后果] 实现了真正的“硬件级失效”。当 Guest 下一次访问该地址时，硬件会产生 EPT 违例并陷入内核，保证不会读到旧数据。
 */
static void k_invalidate_local(uint64_t gpa) {
    // 1. 先从内核元数据索引中删除（防止新的 Push 进来）
    k_invalidate_meta_atomic(gpa);
    
    // 2. [关键] 必须调用这个！触发 unmap_mapping_range
    // 这会强制撤销 Guest 的物理页映射，并冲刷 TLB
    // 由于此函数可能在软中断上下文，必须走 Workqueue 异步执行
    schedule_async_unmap(gpa);
    
    k_log("[Consistency] Invalidate hardware mapping for GPA: %llx", gpa);
}

// --- Driver & Module Init ---
static struct dsm_driver_ops k_ops = {
    .alloc_large_table = k_alloc_large_table,
    .free_large_table = k_free_large_table,
    .alloc_packet = k_alloc_packet,
    .free_packet = k_free_packet,
    .set_gateway_ip = k_set_gateway_ip,
    .send_packet = k_send_packet,
    .fetch_page = k_fetch_page,
    .invalidate_local = k_invalidate_local,
    .log = k_log,
    .is_atomic_context = k_is_atomic_context,
    .touch_watchdog = k_touch_watchdog,
    .alloc_req_id = k_alloc_req_id,
    .free_req_id = k_free_req_id,
    .get_time_us = k_get_time_us,
    .time_diff_us = k_time_diff_us,
    .check_req_status = k_check_req_status,
    .cpu_relax = k_cpu_relax,
    .get_random = k_get_random,
    .yield_cpu_short_time = k_yield_short,
    .send_packet_async = k_send_packet_async
};

// [V29] 存储全局 mapping 以便 unmap 使用
static int wvm_open(struct inode *inode, struct file *filp) {
    g_mapping = inode->i_mapping;
    return 0;
}

static int wvm_release(struct inode *inode, struct file *filp) {
    // 当 QEMU 进程被杀或退出时，VMA 映射(mapping)即将失效。
    // 必须立刻将全局指针置空，防止正在 Workqueue 中排队的 unmap 任务访问野指针。
    down_write(&g_mapping_sem); // 写锁
    g_mapping = NULL;
    up_write(&g_mapping_sem);
    
    return 0;
}

static long wvm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    void __user *argp = (void __user *)arg;
    switch (cmd) {
    case IOCTL_SET_GATEWAY: {
        struct wvm_ioctl_gateway gw;
        if (copy_from_user(&gw, argp, sizeof(gw))) return -EFAULT;
        if (gw.gw_id < WVM_MAX_GATEWAYS) {
            gateway_table[gw.gw_id].sin_family = AF_INET;
            gateway_table[gw.gw_id].sin_addr.s_addr = gw.ip;
            gateway_table[gw.gw_id].sin_port = gw.port;
        }
        break;
    }
    case IOCTL_WVM_REMOTE_RUN: {
        struct wvm_ioctl_remote_run remote_run;
        struct wvm_ipc_cpu_run_req *req = &remote_run.req;
        struct wvm_ipc_cpu_run_ack *ack = &remote_run.ack;

        if (copy_from_user(&remote_run, argp, sizeof(remote_run))) {
            return -EFAULT;
        }

        uint32_t target = req->slave_id;
        if (!WVM_IS_VALID_TARGET(target)) {
            target = wvm_get_compute_slave_id(req->vcpu_index);
        }
        if (!WVM_IS_VALID_TARGET(target)) {
            static int dbg_nodev = 0;
            if (dbg_nodev < 5) {
                uint32_t raw = wvm_get_cpu_mapping_raw(req->vcpu_index);
                printk(KERN_WARNING "[wavevm] REMOTE_RUN cpu=%d raw_route=%u resolved=%u => ENODEV\n",
                       req->vcpu_index, raw, target);
                dbg_nodev++;
            }
            return -ENODEV;
        }

        /*
         * Preserve vcpu_index and routing metadata for KVM too.  A compact
         * context payload cannot identify the guest vCPU on the slave.
         */
        memset(ack, 0, sizeof(*ack));
        int ret = wvm_rpc_call(MSG_VCPU_RUN, req, sizeof(*req),
                               target, ack, sizeof(*ack));
        if (ret < 0) ack->status = ret;
        if (copy_to_user(argp, &remote_run, sizeof(remote_run))) {
            return -EFAULT;
        }
        break;
    }
    case IOCTL_WAIT_IRQ: {
        if (wait_event_interruptible(g_irq_wait_queue, atomic_read(&g_irq_pending) != 0))
            return -ERESTARTSYS;
        atomic_set(&g_irq_pending, 0);
        uint32_t irq = 16; 
        if (copy_to_user(argp, &irq, sizeof(irq))) return -EFAULT;
        break;
    }

    case IOCTL_SET_MEM_LAYOUT: {
        struct wvm_ioctl_mem_layout layout;
        if (copy_from_user(&layout, argp, sizeof(layout))) return -EFAULT;
        return wvm_set_mem_layout(&layout);
    }

    case IOCTL_UPDATE_MEM_ROUTE: {
        struct wvm_ioctl_route_update head;
        
        // 1. 读取头部元数据
        if (copy_from_user(&head, argp, sizeof(head))) return -EFAULT;
        
        // 安全检查：参数注入通常只有几个整数，限制数量防止滥用
        if (head.count > 1024) return -EINVAL;

        // 2. 分配临时缓冲区
        uint32_t *buf = vmalloc(head.count * sizeof(uint32_t));
        if (!buf) return -ENOMEM;

        // 3. 读取 Payload (具体的数值)
        if (copy_from_user(buf, (uint8_t*)argp + sizeof(head), head.count * sizeof(uint32_t))) {
            vfree(buf);
            return -EFAULT;
        }

        // 4. 传导给 Logic Core
        for (int i = 0; i < head.count; i++) {
            // start_index 即 Slot ID
            // Logic Core 会根据 slot 0/1 更新 g_total_nodes / g_my_node_id
            // [FIX-M5] 移除 uint16_t 截断，保留完整 uint32_t
            wvm_set_mem_mapping(head.start_index + i, buf[i]);
        }
        
        vfree(buf);
        break;
    }

    case IOCTL_UPDATE_MEMORY_PLACEMENT: {
        struct wvm_ioctl_route_update head;
        uint32_t *buf;
        size_t bytes;

        if (copy_from_user(&head, argp, sizeof(head))) return -EFAULT;
        if (head.count > WVM_MEMORY_ROUTE_TABLE_SIZE ||
            head.start_index >= WVM_MEMORY_ROUTE_TABLE_SIZE ||
            head.start_index + head.count > WVM_MEMORY_ROUTE_TABLE_SIZE) {
            return -EINVAL;
        }

        bytes = (size_t)head.count * sizeof(uint32_t);
        if (head.count != 0 && bytes / sizeof(uint32_t) != head.count) {
            return -EINVAL;
        }
        buf = vmalloc(bytes);
        if (!buf && bytes != 0) return -ENOMEM;
        if (bytes != 0 &&
            copy_from_user(buf, (uint8_t *)argp + sizeof(head), bytes)) {
            vfree(buf);
            return -EFAULT;
        }

        for (uint32_t i = 0; i < head.count; i++) {
            wvm_set_memory_mapping(head.start_index + i, buf[i]);
        }
        vfree(buf);
        break;
    }

    case IOCTL_SET_VM_ID: {
        uint8_t vm_id;

        if (copy_from_user(&vm_id, argp, sizeof(vm_id))) return -EFAULT;
        g_my_vm_id = vm_id;
        break;
    }

    // [PATCH] 处理 Epoch 更新
    case IOCTL_UPDATE_EPOCH: {
        uint32_t new_epoch;
        if (copy_from_user(&new_epoch, (void __user *)arg, sizeof(uint32_t)))
            return -EFAULT;
        g_kernel_epoch = new_epoch;
        // printk(KERN_INFO "[WVM] Kernel Epoch updated to %u\n", g_kernel_epoch);
        break;
    }

    case IOCTL_UPDATE_CPU_ROUTE: {
        struct wvm_ioctl_route_update head;
        if (copy_from_user(&head, argp, sizeof(head))) return -EFAULT;
        if (head.count > 4096 || head.start_index >= 4096) return -EINVAL;
        if (head.start_index + head.count > 4096) return -EINVAL;

        size_t bytes = (size_t)head.count * sizeof(uint32_t);
        if (head.count != 0 && bytes / sizeof(uint32_t) != head.count) return -EINVAL;
        uint32_t *buf = vmalloc(bytes);
        if (!buf) return -ENOMEM;
        if (copy_from_user(buf, (uint8_t *)argp + sizeof(head), head.count * sizeof(uint32_t))) {
            vfree(buf);
            return -EFAULT;
        }

        for (uint32_t i = 0; i < head.count; i++) {
            wvm_set_cpu_mapping((int)(head.start_index + i), buf[i]);
        }
        if (head.start_index == 0 && head.count >= 3) {
            printk(KERN_INFO "[wavevm] CPU route table updated: [0]=%u [1]=%u [2]=%u (total chunk=%u)\n",
                   buf[0], buf[1], buf[2], head.count);
        }
        vfree(buf);
        break;
    }

    default: return -EINVAL;
    }
    return 0;
}

static const struct vm_operations_struct wvm_vm_ops = {
    .fault = wvm_fault_handler,
    .page_mkwrite = wvm_page_mkwrite, // [V29] 注册 mkwrite 钩子
};

static int wvm_mmap(struct file *filp, struct vm_area_struct *vma) {
    vma->vm_ops = &wvm_vm_ops;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
    vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
#else
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
#endif
    down_write(&g_mapping_sem);
    g_mapping = vma->vm_file->f_mapping; 
    up_write(&g_mapping_sem);
    return 0;
}

static const struct file_operations wvm_fops = {
    .owner = THIS_MODULE,
    .open = wvm_open,
    .release = wvm_release,
    .mmap = wvm_mmap,
    .unlocked_ioctl = wvm_ioctl,
};

static struct miscdevice wvm_misc = { 
    .minor = MISC_DYNAMIC_MINOR, 
    .name = DRIVER_NAME, 
    .fops = &wvm_fops 
};

static int __init wavevm_init(void) {
    int cpu;
    struct sockaddr_in bind_addr;

    init_waitqueue_head(&g_irq_wait_queue);
    init_waitqueue_head(&g_tx_wq);
    init_waitqueue_head(&g_diff_wq); // [V29] Init diff waiter
    spin_lock_init(&g_diff_lock);
    init_rwsem(&g_mapping_sem);

    /*
     * [FIX] 必须用 nr_cpu_ids 而非 num_online_cpus()。
     * get_cpu() 返回的是逻辑 CPU ID（可能不连续），例如 CPU 0,1,4,5 在线时
     * num_online_cpus()=4 但 get_cpu() 可能返回 5，导致 combined_idx 越界。
     * nr_cpu_ids 是内核保证的最大 CPU ID + 1，覆盖所有可能的 get_cpu() 返回值。
     */
    g_req_ctx_count = (size_t)nr_cpu_ids * (size_t)MAX_IDS_PER_CPU;
    g_req_ctx = vzalloc(sizeof(struct req_ctx_t) * g_req_ctx_count);
    if (!g_req_ctx) return -ENOMEM;
    for (size_t i = 0; i < g_req_ctx_count; i++) init_waitqueue_head(&g_req_ctx[i].wq);

    for_each_online_cpu(cpu) {
        struct id_pool_t *pool = per_cpu_ptr(&g_id_pool, cpu);
        spin_lock_init(&pool->lock);
        pool->ids = vzalloc(sizeof(uint32_t) * MAX_IDS_PER_CPU);
        pool->head = 0; pool->tail = MAX_IDS_PER_CPU;
        for (uint32_t i = 0; i < MAX_IDS_PER_CPU; i++) pool->ids[i] = i; 
    }

    size_t slab_size = sizeof(struct wvm_header) + 4096 + sizeof(struct wvm_mem_ack_payload);
    wvm_pkt_cache = kmem_cache_create("wvm_data", slab_size, 0, SLAB_HWCACHE_ALIGN, NULL);

    init_ring(&g_fast_ring);
    init_ring(&g_slow_ring);

    g_tx_thread = kthread_run(tx_worker_thread_fn, NULL, "wavevm_qos_tx");
    
    // [V29] 启动 Diff 提交线程
    g_committer_thread = kthread_run(committer_thread_fn, NULL, "wvm_diff_commit");

    if (wvm_core_init(&k_ops, 1) != 0) return -ENOMEM;

    if (misc_register(&wvm_misc)) return -ENODEV;
    if (sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, IPPROTO_UDP, &g_socket) < 0) return -EIO;

    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(service_port);
    /*
     * kernel_bind() changed its typed sockaddr parameter in newer kernels.
     * The concrete object is always sockaddr_in; void * preserves source
     * compatibility with both the old sockaddr and new sockaddr_unsized APIs.
     */
    kernel_bind(g_socket, (void *)&bind_addr, sizeof(bind_addr));
    g_socket->sk->sk_data_ready = wavevm_udp_data_ready;

    k_log("WaveVM V29.5 'Wavelet' Kernel Backend Loaded (Mode A Active).");
    return 0;
}

static void __exit wavevm_exit(void) {
    int cpu;
    
    // 1. 先停掉所有自产的内核线程
    if (g_tx_thread) kthread_stop(g_tx_thread);
    if (g_committer_thread) kthread_stop(g_committer_thread);
    
    // 2. 刷新系统工作队列 (Workqueue Flush)
    // 确保所有 schedule_async_unmap 扔出去的任务都已执行完毕。
    // 如果不加这行，卸载模块后，系统队列里可能还有任务在跑，会执行已被卸载的代码 -> 崩溃。
    flush_scheduled_work(); 

    // 3. 等待 RCU 回调 (RCU Barrier)
    // 确保所有 kfree_rcu 的内存都已真正释放。
    rcu_barrier(); 

    // 4. 释放其余资源
    if (g_fast_ring.slots) vfree(g_fast_ring.slots);
    if (g_slow_ring.slots) vfree(g_slow_ring.slots);

    if (g_socket) { g_socket->sk->sk_data_ready = NULL; sock_release(g_socket); }
    misc_deregister(&wvm_misc);
    
    for_each_possible_cpu(cpu) {
        struct id_pool_t *pool = per_cpu_ptr(&g_id_pool, cpu);
        if (pool->ids) vfree(pool->ids);
    }
    vfree(g_req_ctx);
    // [FIX-M4] 销毁 slab 缓存，防止模块卸载后内存泄漏
    if (wvm_pkt_cache) kmem_cache_destroy(wvm_pkt_cache);
}

module_init(wavevm_init);
module_exit(wavevm_exit);
MODULE_LICENSE("GPL");
