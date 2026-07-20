
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "sysemu/cpus.h"
#include "sysemu/hw_accel.h"
#include "sysemu/kvm.h" 
#include "sysemu/runstate.h"
#include "qapi/error.h"
#include "linux/kvm.h"
#include <signal.h>
#include <time.h>
#include <sys/syscall.h>
#include "hw/boards.h"
#include "qemu/main-loop.h"
#include "exec/address-spaces.h"
#include "exec/cpu-all.h"
#include "hw/i386/apic.h"
#include "../kvm/kvm-cpus.h"
#include "../../../common_include/wavevm_protocol.h" 
#include "../../../common_include/wavevm_config.h"
#include "../../../common_include/wavevm_ioctl.h"
#include "wavevm-accel.h"
#include "qemu/thread.h"
#include "qemu/guest-random.h"
#include "tcg/tcg.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <limits.h>

extern void wavevm_apply_split_hint(int split);

// Per-vCPU Socket Pool to eliminate lock contention
static int *g_vcpu_socks = NULL;
static int g_configured_vcpus = 0;
static int g_wvm_debug_once = 0;

/* No-op signal handler for SIGRTMIN (HALT-WAKE safety timer).
 * Must be a real handler (not SIG_IGN) so the signal sets
 * signal_pending and can interrupt KVM_RUN. */
static void wvm_halt_wake_nop_handler(int sig) { (void)sig; }

/* [FIX] Track per-AP SIPI trampoline completion.
 * After SIPI, APs must run the BIOS trampoline LOCALLY first (to increment
 * the SMP CPU counter in shared memory and HLT). Only after this initial
 * local run should APs be dispatched to remote slaves.
 * ap_did_local_sipi[cpu_index] = 1 means trampoline is done. */
#define MAX_VCPUS 64
static int ap_did_local_sipi[MAX_VCPUS] = {0};
static int ap_did_os_sipi[MAX_VCPUS] = {0};
static gint64 ap_halt_probe_us[MAX_VCPUS] = {0};
/* HLT exit with an injected LAPIC timer/interrupt needs one local KVM
 * re-entry so the pending event can be consumed instead of being replayed
 * as the same HLT loop again. */
static int ap_hlt_pending_irq[MAX_VCPUS] = {0};

static void wavevm_try_import_split_from_peer(int sock)
{
#ifdef SO_PEERCRED
    struct ucred cred;
    socklen_t cred_len = sizeof(cred);
    char path[64];
    FILE *fp;
    char envbuf[8192];
    size_t n;
    size_t i = 0;

    if (getsockopt(sock, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) < 0) {
        return;
    }

    snprintf(path, sizeof(path), "/proc/%ld/environ", (long)cred.pid);
    fp = fopen(path, "rb");
    if (!fp) {
        return;
    }

    n = fread(envbuf, 1, sizeof(envbuf) - 1, fp);
    fclose(fp);
    if (n == 0) {
        return;
    }
    envbuf[n] = '\0';

    while (i < n) {
        char *kv = &envbuf[i];
        size_t kv_len = strnlen(kv, n - i);

        if (kv_len == 0) {
            i++;
            continue;
        }

        if (strncmp(kv, "WVM_LOCAL_SPLIT=", 16) == 0) {
            char *endptr = NULL;
            long parsed = strtol(kv + 16, &endptr, 10);
            if (endptr && *endptr == '\0' && parsed >= 0 && parsed <= INT_MAX) {
                wavevm_apply_split_hint((int)parsed);
            }
            break;
        }

        i += kv_len + 1;
    }
#else
    (void)sock;
#endif
}

static int connect_to_master_helper(void)
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    const char *env_path;
    char fallback[128];

    if (sock < 0) {
        return -1;
    }

    env_path = getenv("WVM_ENV_SOCK_PATH");
    if (!env_path) {
        const char *inst_id = getenv("WVM_INSTANCE_ID");
        snprintf(fallback, sizeof(fallback), "/tmp/wvm_user_%s.sock",
                 inst_id ? inst_id : "0");
        env_path = fallback;
    }

    strncpy(addr.sun_path, env_path, sizeof(addr.sun_path) - 1);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    wavevm_try_import_split_from_peer(sock);
    return sock;
}

static int write_full(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static int read_full(int fd, void *buf, size_t len)
{
    uint8_t *p = buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = read(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

// TCG Helper Declarations (Defined in wavevm-tcg.c)
extern void wvm_tcg_get_state(CPUState *cpu, wvm_tcg_context_t *ctx);
extern void wvm_tcg_set_state(CPUState *cpu, wvm_tcg_context_t *ctx);
extern void wavevm_slave_vcpu_loop(CPUState *cpu);
static pthread_once_t g_wavevm_tcg_region_once = PTHREAD_ONCE_INIT;

static void wavevm_init_tcg_region_once(void)
{
    /* Enable MTTCG-style region allocation so each vCPU thread gets its
     * own code-gen region.  Without this, tcg_n_regions() returns 1 and
     * only one thread can tcg_register_thread() without asserting.
     *
     * tcg_n_regions() checks mttcg_enabled (not parallel_cpus), so we
     * must set the correct global.  parallel_cpus is also needed for
     * some TCG code-path decisions. */
    extern bool mttcg_enabled;
    extern bool parallel_cpus;
    mttcg_enabled = true;
    parallel_cpus = true;
    tcg_region_init();
}

struct wavevm_policy_ops {
    int (*schedule_policy)(int cpu_index);
};

// [Policy] Tiered Scheduling: Local vs Remote
static int remote_rpc_policy(int cpu_index) {
    //不再使用 WVM_LOCAL_CPU_COUNT 宏，而是使用动态变量
    if (cpu_index >= g_wvm_local_split) return 1; // 远程执行
    return 0; // 本地执行
}

static struct wavevm_policy_ops ops = { .schedule_policy = remote_rpc_policy };

static bool wavevm_valid_io_exit(const struct kvm_run *run)
{
    if (run->io.size != 1 && run->io.size != 2 &&
        run->io.size != 4 && run->io.size != 8) {
        return false;
    }
    if (run->io.count == 0 || run->io.count > 8) {
        return false;
    }
    return true;
}

static bool wavevm_valid_mmio_exit(const struct kvm_run *run)
{
    return run->mmio.len == 1 || run->mmio.len == 2 ||
           run->mmio.len == 4 || run->mmio.len == 8;
}

static inline uint32_t wavevm_lapic_reg32(const struct kvm_lapic_state *lapic, uint32_t off)
{
    uint32_t v = 0;
    if (off + sizeof(v) <= sizeof(lapic->regs)) {
        memcpy(&v, &lapic->regs[off], sizeof(v));
    }
    return v;
}

/* Keep local APIC/interrupt state and only import architectural sregs fields. */
static void wavevm_apply_arch_sregs(struct kvm_sregs *dst,
                                    const struct kvm_sregs *src)
{
    dst->cs = src->cs;
    dst->ds = src->ds;
    dst->es = src->es;
    dst->fs = src->fs;
    dst->gs = src->gs;
    dst->ss = src->ss;
    dst->tr = src->tr;
    dst->ldt = src->ldt;
    dst->gdt = src->gdt;
    dst->idt = src->idt;
    dst->cr0 = src->cr0;
    dst->cr2 = src->cr2;
    dst->cr3 = src->cr3;
    dst->cr4 = src->cr4;
    dst->cr8 = src->cr8;
    dst->efer = src->efer;
}

/* 
 * [物理意图] 充当远程 Slave 的“本地代理执行人”。
 * [关键逻辑] 当远程计算节点触发 PIO/MMIO 退出时，Master 在本地 QEMU 中代为执行该 I/O 操作并返回结果。
 * [后果] 解决了 I/O 设备物理位置的透明性。它让 Guest 以为显卡就在本地，即便真实的读写指令是在千里之外执行的。
 */
static void wavevm_handle_io(CPUState *cpu) {
    struct kvm_run *run = cpu->kvm_run;
    uint16_t port = run->io.port;
    void *data = (uint8_t *)run + run->io.data_offset;
    int is_write = (run->io.direction == KVM_EXIT_IO_OUT);

    /* [FIX] 处理 string I/O (REP INS/OUTS)：count 可能 > 1，需逐个迭代 */
    for (uint32_t i = 0; i < run->io.count; i++) {
        address_space_rw(&address_space_io, port, MEMTXATTRS_UNSPECIFIED,
                         data, run->io.size, is_write);
        data = (uint8_t *)data + run->io.size;
    }
}

static void wavevm_handle_mmio(CPUState *cpu) {
    struct kvm_run *run = cpu->kvm_run;
    hwaddr addr = run->mmio.phys_addr;
    void *data = run->mmio.data;

    address_space_rw(&address_space_memory, addr, MEMTXATTRS_UNSPECIFIED,
                     data, run->mmio.len,
                     run->mmio.is_write);
}

static void wavevm_tcg_run_local_wake(CPUState *cpu, X86CPU *x86cpu, int ci)
{
    CPUState *cs = CPU(x86cpu);
    static int local_wake_dbg[MAX_VCPUS] = {0};
    int tr = -1;
    bool log_wake = local_wake_dbg[ci] < 20;

    if (log_wake) {
        fprintf(stderr,
                "[WVM-TCG-WAKE] cpu=%d local wake exec intreq=0x%x\n",
                cpu->cpu_index, (unsigned)cs->interrupt_request);
        local_wake_dbg[ci]++;
    }

    qemu_mutex_lock_iothread();
    /*
     * This is a single native-TCG-style local entry for a master-owned
     * LAPIC/PIC wakeup.  Execution boundaries must come from QEMU events,
     * not a wall-clock kick: a fixed delay races guest timing and changes
     * whether the following slice is routed remotely.
     */
    cpu->exception_index = -1;
    qatomic_mb_set(&cpu->exit_request, 0);
    qemu_mutex_unlock_iothread();
    cpu_exec_start(cpu);
    tr = cpu_exec(cpu);
    cpu_exec_end(cpu);
    qemu_mutex_lock_iothread();

    if (tr == EXCP_ATOMIC) {
        qemu_mutex_unlock_iothread();
        cpu_exec_step_atomic(cpu);
        qemu_mutex_lock_iothread();
    } else if (tr == EXCP_HLT || tr == EXCP_HALTED || cpu->halted) {
        cpu->halted = 1;
        cpu->exception_index = -1;
    }
    if (log_wake) {
        CPUX86State *denv = &x86cpu->env;
        uint32_t eflags = cpu_compute_eflags(denv);
        fprintf(stderr,
                "[WVM-TCG-WAKE] cpu=%d local wake ret=%d iters=%d "
                "interrupts=%d rip=0x%lx cs=0x%x eflags=0x%x "
                "hflags=0x%x hflags2=0x%x halted=%d intreq=0x%x\n",
                cpu->cpu_index, tr, 1, tr == EXCP_INTERRUPT,
                (unsigned long)denv->eip,
                (unsigned)denv->segs[R_CS].selector,
                (unsigned)eflags,
                (unsigned)denv->hflags,
                (unsigned)denv->hflags2,
                cpu->halted, (unsigned)cs->interrupt_request);
    }
    qemu_mutex_unlock_iothread();
}

static void wavevm_tcg_deliver_init_sipi(CPUState *cpu, X86CPU *x86cpu)
{
    CPUState *cs = CPU(x86cpu);
    bool had_init = cs->interrupt_request & CPU_INTERRUPT_INIT;

    /*
     * do_cpu_init() enters QEMU's resettable API and must be serialized by
     * the iothread mutex.  wavevm_remote_exec() is intentionally called with
     * that mutex unlocked, so the custom TCG SIPI lifecycle has to take it
     * around the INIT+SIPI pair.
     */
    qemu_mutex_lock_iothread();
    if (had_init) {
        do_cpu_init(x86cpu);
    }
    do_cpu_sipi(x86cpu);  /* sets CS:IP, clears CPU_INTERRUPT_SIPI */
    cpu->exception_index = -1;
    qemu_mutex_unlock_iothread();
}

/* 
 * [物理意图] 实现 CPU 核心的“跨物理节点迁移（Context Serialization）”。
 * [关键逻辑] 将 CPU 的全量寄存器状态、MSR 及时钟计数器进行二进制打包，并发射至远程 Slave。
 * [后果] 实现了算力的透明流动。若序列化逻辑不严谨（如漏掉 rflags），远程执行会立即触发 Guest Kernel Panic。
 */
static void wavevm_remote_exec(CPUState *cpu) {
    // 动态边界检查
    if (cpu->cpu_index >= g_configured_vcpus) return;

    /* [FIX] AP vCPU lifecycle gate:
     * 1. APs start UNINITIALIZED → wait (halted) until BSP sends SIPI.
     * 2. KVM handles SIPI atomically: AP goes UNINITIALIZED → RUNNABLE
     *    (we never see the transient SIPI_RECEIVED state).
     * 3. When AP first becomes RUNNABLE, signal caller to do a LOCAL
     *    kvm_cpu_exec() for the BIOS trampoline (increments SMP counter,
     *    then HLTs). This MUST run locally so shared-memory writes are
     *    visible to the BSP.
     * 4. After trampoline HLT, AP stays parked until OS sends IPI.
     * 5. Once ap_did_local_sipi is set, future dispatches go remote. */
    if (kvm_enabled()) {
        struct kvm_mp_state mp;
        int mp_ret = ioctl(cpu->kvm_fd, KVM_GET_MP_STATE, &mp);
        {
            static int mp_dbg[MAX_VCPUS] = {0};
            int ci = cpu->cpu_index < MAX_VCPUS ? cpu->cpu_index : 0;
            if (mp_dbg[ci] < 30) {
                fprintf(stderr, "[WVM-MP] cpu=%d mp_ret=%d mp_state=%d sipi_done=%d\n",
                        cpu->cpu_index, mp_ret,
                        mp_ret == 0 ? mp.mp_state : -1,
                        (cpu->cpu_index < MAX_VCPUS) ? ap_did_local_sipi[cpu->cpu_index] : -1);
                mp_dbg[ci]++;
            }
        }
        if (mp_ret == 0) {
            if (mp.mp_state != KVM_MP_STATE_RUNNABLE) {
                /* AP is HALTED after OS-SIPI completed: Linux scheduler may
                 * send an IPI to wake it. KVM_GET_MP_STATE does NOT process
                 * pending LAPIC events — only KVM_RUN does. So we must do a
                 * real kvm_cpu_exec to let KVM deliver the wake-up IPI. */
                if (mp.mp_state == KVM_MP_STATE_HALTED &&
                    cpu->cpu_index < MAX_VCPUS &&
                    ap_did_os_sipi[cpu->cpu_index]) {
                    bool hlt_pending_irq = ap_hlt_pending_irq[cpu->cpu_index];

                    if (!hlt_pending_irq &&
                        cpu->interrupt_request == 0 &&
                        !cpu_has_work(cpu)) {
                        gint64 now_us = g_get_monotonic_time();
                        gint64 *last_probe = &ap_halt_probe_us[cpu->cpu_index];

                        if (*last_probe != 0 && (now_us - *last_probe) < 5000000) {
                            cpu->halted = 1;
                            return;
                        }
                        *last_probe = now_us;
                    }

                    /* HALT-WAKE: Pipeline #40 approach with critical fix.
                     *
                     * Use full kvm_cpu_exec() with a bounded timer.
                     * CRITICAL: Do NOT set mp_state to HALTED here!
                     * If we force HALTED, KVM puts the AP directly into
                     * vcpu_block. The LAPIC timer was never armed on the
                     * master's KVM (it was armed on the slave during
                     * OS-SIPI), so vcpu_block has no pending interrupt
                     * to wake on. The AP sleeps until SIGUSR1 with
                     * 0 timer ticks -> RCU stall.
                     *
                     * Instead, set RUNNABLE and let the AP execute its
                     * idle loop naturally: HLT instruction -> KVM handles
                     * HLT internally -> vcpu_block -> LAPIC timer fires
                     * -> timer handler re-arms -> HLT -> ... This keeps
                     * timer ticks flowing continuously.
                     */

                    static int halt_wake_dbg[MAX_VCPUS] = {0};
                    int ci = cpu->cpu_index;

                    if (halt_wake_dbg[ci] < 30) {
                        fprintf(stderr, "[WVM-HALT-WAKE] cpu=%d entering "
                                "kvm_cpu_exec (2s timer) mp_in=%d\n",
                                ci, mp.mp_state);
                    }

                    bool had_wake_source = hlt_pending_irq ||
                                            (cpu->interrupt_request != 0 ||
                                             cpu_has_work(cpu));

                    /* Set RUNNABLE so AP executes idle loop naturally.
                     * The idle loop will HLT, and KVM handles that
                     * internally when kvm_halt_in_kernel() is true. */
                    cpu->halted = 0;
                    {
                        X86CPU *x86cpu = X86_CPU(cpu);
                        x86cpu->env.mp_state = KVM_MP_STATE_RUNNABLE;
                    }

                    /* 2s SIGUSR1 timer to bound local execution.
                     * kvm_cpu_exec handles SIGUSR1 gracefully (kvm_eat_signals,
                     * returns EXCP_INTERRUPT). 2s is enough for LAPIC timer
                     * to establish fire-rearm cycle and process IPIs. */
                    timer_t hw_timer;
                    struct sigevent sev;
                    memset(&sev, 0, sizeof(sev));
                    sev.sigev_notify = SIGEV_THREAD_ID;
                    sev.sigev_signo = SIGUSR1;
                    sev._sigev_un._tid = syscall(SYS_gettid);
                    int timer_ok = (timer_create(CLOCK_MONOTONIC, &sev,
                                                 &hw_timer) == 0);
                    if (timer_ok) {
                        struct itimerspec its = {0};
                        its.it_value.tv_sec = 5;  /* give the guest more room to service timers */
                        timer_settime(hw_timer, 0, &its, NULL);
                    }

                    /* kvm_cpu_exec expects iothread locked (it unlocks
                     * before KVM_RUN and relocks after). wavevm_remote_exec
                     * is called with iothread UNlocked, so lock here. */
                    qemu_mutex_lock_iothread();
                    int wr = kvm_cpu_exec(cpu);
                    qemu_mutex_unlock_iothread();

                    if (timer_ok) {
                        struct itimerspec its = {0};
                        timer_settime(hw_timer, 0, &its, NULL);
                        timer_delete(hw_timer);
                    }

                    ioctl(cpu->kvm_fd, KVM_GET_MP_STATE, &mp);
                    if (halt_wake_dbg[ci] < 20) {
                        fprintf(stderr, "[WVM-HALT-WAKE] cpu=%d ret=%d "
                                "mp=%d halted=%d irq=0x%x has_work=%d\n",
                                ci, wr, mp.mp_state, cpu->halted,
                                cpu->interrupt_request, cpu_has_work(cpu));
                        halt_wake_dbg[ci]++;
                    }

                    if (mp.mp_state == KVM_MP_STATE_RUNNABLE ||
                        had_wake_source ||
                        (!cpu->halted && mp.mp_state != KVM_MP_STATE_HALTED)) {
                        /* AP was woken by IPI and is doing real work.
                         * Sync state and dispatch to slave. */
                        if (ci < MAX_VCPUS) {
                            ap_hlt_pending_irq[ci] = 0;
                        }
                        cpu->halted = 0;
                        {
                            X86CPU *x86cpu = X86_CPU(cpu);
                            x86cpu->env.mp_state = KVM_MP_STATE_RUNNABLE;
                        }
                        qemu_mutex_lock_iothread();
                        cpu_synchronize_state(cpu);
                        qemu_mutex_unlock_iothread();
                        /* Fall through to remote dispatch */
                    } else {
                        /* AP is still idle (processed timer ticks but no
                         * real work). Return and retry next iteration. */
                        if (ci < MAX_VCPUS) {
                            ap_hlt_pending_irq[ci] = 0;
                        }
                        cpu->halted = 1;
                        return;
                    }
                } else if (mp.mp_state == KVM_MP_STATE_INIT_RECEIVED &&
                           cpu->cpu_index < MAX_VCPUS &&
                           !ap_did_local_sipi[cpu->cpu_index]) {
                    /* BIOS-SIPI delivery fix (V33p):
                     *
                     * KVM_GET_MP_STATE does NOT process pending LAPIC events.
                     * BSP sends INIT+SIPI via LAPIC ICR, KVM queues the SIPI
                     * on this vCPU, but the state won't transition from
                     * INIT_RECEIVED → RUNNABLE until we enter KVM_RUN.
                     *
                     * Without this, the AP polls mp_state forever seeing
                     * INIT_RECEIVED (value 1), never processing the queued
                     * SIPI. This is an intermittent race: sometimes KVM
                     * processes the SIPI before we start polling (and it
                     * works), sometimes it doesn't (and CPU#N hangs).
                     *
                     * Fix: do a bounded KVM_RUN. KVM will wait for SIPI
                     * in INIT_RECEIVED state. When SIPI arrives, the vCPU
                     * transitions to RUNNABLE and starts executing. We use
                     * a 5s timer to avoid blocking forever if SIPI hasn't
                     * been sent yet (early in boot, BSP may not have reached
                     * the AP bringup code). */

                    static int sipi_wait_dbg[MAX_VCPUS] = {0};
                    int ci = cpu->cpu_index;

                    if (sipi_wait_dbg[ci] < 10) {
                        fprintf(stderr, "[WVM-SIPI-WAIT] cpu=%d INIT_RECEIVED, "
                                "entering KVM_RUN for SIPI delivery\n", ci);
                        sipi_wait_dbg[ci]++;
                    }

                    /* Create per-thread timer: 5s to receive SIPI.
                     * If BSP hasn't sent SIPI yet, we'll time out and retry
                     * on the next iteration. */
                    timer_t sipi_wait_timer;
                    struct sigevent sev;
                    memset(&sev, 0, sizeof(sev));
                    sev.sigev_notify = SIGEV_THREAD_ID;
                    sev.sigev_signo = SIGUSR1;
                    sev._sigev_un._tid = syscall(SYS_gettid);
                    int timer_ok = (timer_create(CLOCK_MONOTONIC, &sev,
                                                 &sipi_wait_timer) == 0);
                    if (timer_ok) {
                        struct itimerspec its = {0};
                        its.it_value.tv_sec = 5;
                        timer_settime(sipi_wait_timer, 0, &its, NULL);
                    }

                    /* KVM_RUN in INIT_RECEIVED state: KVM will wait for
                     * SIPI. When SIPI arrives, AP transitions to RUNNABLE
                     * and executes from the SIPI vector address.
                     *
                     * CRITICAL: Do NOT hold the BQL (iothread lock) here.
                     * BSP needs the BQL to execute kvm_cpu_exec() which
                     * sends the SIPI via LAPIC ICR. If we hold BQL while
                     * waiting for SIPI, BSP can't run → SIPI never sent
                     * → 5s timeout → repeat = livelock. */
                    int wr = ioctl(cpu->kvm_fd, KVM_RUN, 0);

                    if (timer_ok) {
                        struct itimerspec its = {0};
                        timer_settime(sipi_wait_timer, 0, &its, NULL);
                        timer_delete(sipi_wait_timer);
                    }

                    /* Re-check mp_state after KVM_RUN */
                    ioctl(cpu->kvm_fd, KVM_GET_MP_STATE, &mp);

                    if (sipi_wait_dbg[ci] < 20) {
                        fprintf(stderr, "[WVM-SIPI-WAIT] cpu=%d KVM_RUN ret=%d "
                                "mp=%d halted=%d\n", ci, wr, mp.mp_state,
                                cpu->halted);
                        sipi_wait_dbg[ci]++;
                    }

                    if (mp.mp_state == KVM_MP_STATE_RUNNABLE) {
                        /* SIPI delivered, AP is executing (trampoline in
                         * progress). Signal caller to run local trampoline
                         * via the existing 0x10003 path. */
                        fprintf(stderr, "[WVM-SIPI-WAIT] cpu=%d SIPI delivered, "
                                "now RUNNABLE\n", ci);
                        cpu->halted = 1;
                        cpu->exception_index = 0x10003;
                        return;
                    } else if (mp.mp_state == KVM_MP_STATE_HALTED) {
                        /* Trampoline already completed inside our KVM_RUN
                         * (SIPI arrived → executed trampoline → HLT).
                         * Mark trampoline done directly — do NOT signal
                         * 0x10003 or the trampoline would run twice. */
                        fprintf(stderr, "[WVM-SIPI-WAIT] cpu=%d trampoline "
                                "completed inside KVM_RUN, marking done\n", ci);
                        ap_did_local_sipi[ci] = 1;
                        cpu->halted = 1;
                        return;
                    }

                    /* SIPI not yet delivered (timer expired, still
                     * INIT_RECEIVED). Park and retry next iteration. */
                    cpu->halted = 1;
                    return;
                } else {
                    /* AP not yet RUNNABLE (UNINITIALIZED or other pre-SIPI state) */
                    cpu->halted = 1;
                    return;
                }
            }
            /* AP is RUNNABLE. Check if trampoline needs to run locally first. */
            if (cpu->cpu_index < MAX_VCPUS &&
                !ap_did_local_sipi[cpu->cpu_index]) {
                fprintf(stderr, "[WVM-SIPI-LOCAL] cpu=%d first RUNNABLE — signaling local trampoline run\n",
                        cpu->cpu_index);
                cpu->halted = 1;
                cpu->exception_index = 0x10003; /* magic: run trampoline locally */
                return;
            }
        } else {
            fprintf(stderr, "[WVM-MP] cpu=%d KVM_GET_MP_STATE failed! errno=%d\n",
                    cpu->cpu_index, errno);
        }
    } else {
        /* ===== TCG mode AP lifecycle gate =====
         * In TCG mode there is no KVM fd, so we handle SIPI via QEMU's
         * software APIC model.  The flow mirrors the KVM path above:
         *   1. AP starts halted.  BSP sends SIPI → APIC sets
         *      CPU_INTERRUPT_SIPI on the target AP.
         *   2. We detect the pending SIPI here, call do_cpu_sipi() to
         *      initialise CS:IP from the SIPI vector, then run the BIOS
         *      trampoline LOCALLY with cpu_exec().
         *   3. After the trampoline HLTs the AP, we mark
         *      ap_did_local_sipi so future dispatches go remote.
         *   4. For OS-level SIPI (second SIPI after Linux boots), we
         *      similarly run the trampoline locally before remote dispatch.
         */
        int ci = cpu->cpu_index < MAX_VCPUS ? cpu->cpu_index : 0;

        /* 1. Pre-SIPI: AP still waiting for first SIPI from BIOS */
        if (!ap_did_local_sipi[ci]) {
            X86CPU *x86cpu = X86_CPU(cpu);
            CPUState *cs = CPU(x86cpu);

            if (!(cs->interrupt_request & CPU_INTERRUPT_SIPI)) {
                /* No SIPI yet — stay parked.  Clear POLL to avoid
                 * pointless wake/park cycles (51K+ in previous runs). */
                cs->interrupt_request &= ~CPU_INTERRUPT_POLL;
                cpu->halted = 1;
                return;
            }

            /*
             * Deliver INIT before SIPI, matching QEMU's normal interrupt
             * ordering.  INIT restores the APIC wait-for-SIPI state and
             * preserves a simultaneously pending SIPI for the next step.
             */
            fprintf(stderr, "[WVM-TCG-SIPI] cpu=%d SIPI pending, calling do_cpu_sipi "
                    "(sipi_vector from APIC)\n", cpu->cpu_index);
            wavevm_tcg_deliver_init_sipi(cpu, x86cpu);

            {
                CPUX86State *env = &x86cpu->env;
                fprintf(stderr, "[WVM-TCG-SIPI] cpu=%d after SIPI: cs=0x%x cs.base=0x%lx "
                        "eip=0x%lx cr0=0x%lx\n",
                        cpu->cpu_index,
                        (unsigned)env->segs[R_CS].selector,
                        (unsigned long)env->segs[R_CS].base,
                        (unsigned long)env->eip,
                        (unsigned long)env->cr[0]);
            }

            /* Run BIOS SMP trampoline locally.  The trampoline increments
             * the BSP's AP counter in shared memory, then HLTs.  This MUST
             * run locally so the memory write is visible to BSP.
             * Note: wavevm_remote_exec() is called with iothread UNlocked,
             * so we must lock before the unlock/exec/lock cycle. */
            fprintf(stderr, "[WVM-TCG-SIPI] cpu=%d running BIOS trampoline locally\n",
                    cpu->cpu_index);
            cpu->halted = 0;

            /* Clear any stale interrupts that could interfere with trampoline.
             * POLL and RESET can cause cpu_exec() to return immediately without
             * executing guest code, wasting iterations. */
            {
                CPUState *cs2 = CPU(X86_CPU(cpu));
                cs2->interrupt_request &= ~(CPU_INTERRUPT_POLL |
                                            CPU_INTERRUPT_INIT);
            }

            /* Bounded local execution: up to 500ms for trampoline */
            int tramp_iters = 0;
            const int max_tramp_iters = 100;
            qemu_mutex_lock_iothread();
            while (tramp_iters < max_tramp_iters) {
                qemu_mutex_unlock_iothread();
                cpu_exec_start(cpu);
                int tr = cpu_exec(cpu);
                cpu_exec_end(cpu);
                qemu_mutex_lock_iothread();

                /* EXCP_INTERRUPT (0x10000): another vCPU called cpu_exit()
                 * on us — typically from cpu_exec_step_atomic() when a
                 * sibling AP hits a lock-prefix instruction.  This is
                 * benign; we made no guest progress.  Don't count it. */
                if (tr == EXCP_INTERRUPT) {
                    continue;
                }

                /* EXCP_ATOMIC: lock-prefix instruction needs exclusive
                 * execution (MTTCG stops all other vCPUs for one TB).
                 * SeaBIOS trampoline uses lock inc/xchg for AP counter.
                 * Don't count this as a real iteration. */
                if (tr == EXCP_ATOMIC) {
                    qemu_mutex_unlock_iothread();
                    cpu_exec_step_atomic(cpu);
                    qemu_mutex_lock_iothread();
                    continue;
                }

                tramp_iters++;

                /* Diagnostic: log real iterations (not INTERRUPT/ATOMIC noise) */
                {
                    CPUX86State *denv = &X86_CPU(cpu)->env;
                    CPUState *dcs = CPU(X86_CPU(cpu));
                    fprintf(stderr, "[WVM-TCG-SIPI] cpu=%d iter=%d tr=%d "
                            "eip=0x%lx halted=%d intreq=0x%x\n",
                            cpu->cpu_index, tramp_iters, tr,
                            (unsigned long)denv->eip, cpu->halted,
                            (unsigned)dcs->interrupt_request);
                }

                if (cpu->halted) {
                    fprintf(stderr, "[WVM-TCG-SIPI] cpu=%d trampoline HLTed after %d iters\n",
                            cpu->cpu_index, tramp_iters);
                    break;
                }
                if (tr == EXCP_HLT) {
                    cpu->halted = 1;
                    fprintf(stderr, "[WVM-TCG-SIPI] cpu=%d trampoline EXCP_HLT after %d iters\n",
                            cpu->cpu_index, tramp_iters);
                    break;
                }
                if (tr != EXCP_HALTED && tr != EXCP_HLT) {
                    fprintf(stderr, "[WVM-TCG-SIPI] cpu=%d trampoline unexpected tr=0x%x "
                            "at iter=%d eip=0x%lx\n",
                            cpu->cpu_index, (unsigned)tr, tramp_iters,
                            (unsigned long)(&X86_CPU(cpu)->env)->eip);
                }
            }
            if (tramp_iters >= max_tramp_iters) {
                CPUX86State *denv = &X86_CPU(cpu)->env;
                CPUState *dcs = CPU(X86_CPU(cpu));
                fprintf(stderr, "[WVM-TCG-SIPI] cpu=%d trampoline did not HLT after %d iters, "
                        "forcing halt (eip=0x%lx intreq=0x%x)\n",
                        cpu->cpu_index, max_tramp_iters,
                        (unsigned long)denv->eip,
                        (unsigned)dcs->interrupt_request);
                cpu->halted = 1;
            }

            qemu_mutex_unlock_iothread();  /* restore lock-unheld for caller */
            ap_did_local_sipi[ci] = 1;
            fprintf(stderr, "[WVM-TCG-SIPI] cpu=%d BIOS trampoline done, AP parked\n",
                    cpu->cpu_index);
            return;  /* Don't dispatch yet — wait for OS SIPI */
        }

        /* 2. Post-BIOS-SIPI: AP is parked, waiting for OS INIT+SIPI */
        if (!ap_did_os_sipi[ci]) {
            X86CPU *x86cpu = X86_CPU(cpu);
            CPUState *cs = CPU(x86cpu);

            /* Check for OS-level SIPI (Linux sends INIT+SIPI to APs) */
            if (cs->interrupt_request & CPU_INTERRUPT_SIPI) {
                fprintf(stderr, "[WVM-TCG-SIPI] cpu=%d OS SIPI pending\n", cpu->cpu_index);
                wavevm_tcg_deliver_init_sipi(cpu, x86cpu);

                /* Run OS trampoline locally (Linux SMP init) */
                fprintf(stderr, "[WVM-TCG-SIPI] cpu=%d running OS trampoline locally\n",
                        cpu->cpu_index);
                cpu->halted = 0;

                int tramp_iters = 0;
                const int max_tramp_iters = 500;  /* OS trampoline may be longer */
                qemu_mutex_lock_iothread();
                while (tramp_iters < max_tramp_iters) {
                    qemu_mutex_unlock_iothread();
                    cpu_exec_start(cpu);
                    int tr = cpu_exec(cpu);
                    cpu_exec_end(cpu);
                    qemu_mutex_lock_iothread();

                    if (tr == EXCP_INTERRUPT) {
                        continue;  /* kicked by sibling's atomic step */
                    }

                    if (tr == EXCP_ATOMIC) {
                        qemu_mutex_unlock_iothread();
                        cpu_exec_step_atomic(cpu);
                        qemu_mutex_lock_iothread();
                        continue;
                    }

                    tramp_iters++;

                    if (cpu->halted || tr == EXCP_HLT) {
                        CPUX86State *denv = &x86cpu->env;
                        cpu->halted = 1;
                        fprintf(stderr,
                                "[WVM-TCG-SIPI] cpu=%d OS trampoline done after %d iters "
                                "tr=%d rip=0x%lx cs=0x%x cr0=0x%lx efer=0x%lx "
                                "rsp=0x%lx intreq=0x%x hflags=0x%x hflags2=0x%x mp=%d\n",
                                cpu->cpu_index, tramp_iters, tr,
                                (unsigned long)denv->eip,
                                (unsigned)denv->segs[R_CS].selector,
                                (unsigned long)denv->cr[0],
                                (unsigned long)denv->efer,
                                (unsigned long)denv->regs[R_ESP],
                                (unsigned)cs->interrupt_request,
                                (unsigned)denv->hflags,
                                (unsigned)denv->hflags2,
                                denv->mp_state);
                        break;
                    }
                    if (tr != EXCP_HALTED && tr != EXCP_HLT) {
                        fprintf(stderr, "[WVM-TCG-SIPI] cpu=%d OS trampoline unexpected tr=0x%x "
                                "at iter=%d\n", cpu->cpu_index, (unsigned)tr, tramp_iters);
                    }
                }
                if (tramp_iters >= max_tramp_iters) {
                    CPUX86State *denv = &x86cpu->env;
                    fprintf(stderr,
                            "[WVM-TCG-SIPI] cpu=%d OS trampoline timeout after %d iters "
                            "rip=0x%lx cs=0x%x cr0=0x%lx efer=0x%lx "
                            "rsp=0x%lx intreq=0x%x hflags=0x%x hflags2=0x%x mp=%d\n",
                            cpu->cpu_index, max_tramp_iters,
                            (unsigned long)denv->eip,
                            (unsigned)denv->segs[R_CS].selector,
                            (unsigned long)denv->cr[0],
                            (unsigned long)denv->efer,
                            (unsigned long)denv->regs[R_ESP],
                            (unsigned)cs->interrupt_request,
                            (unsigned)denv->hflags,
                            (unsigned)denv->hflags2,
                            denv->mp_state);
                    cpu->halted = 1;
                }

                ap_did_os_sipi[ci] = 1;
                fprintf(stderr, "[WVM-TCG-SIPI] cpu=%d OS trampoline done, ready for remote dispatch\n",
                        cpu->cpu_index);
                qemu_mutex_unlock_iothread();  /* restore lock-unheld for caller */
                return;
            }

            /* No OS SIPI yet — AP stays parked.
             * Only SIPI can advance the AP to the next stage.
             * Ignore POLL/HARD interrupts here — they cause
             * useless wake-up storms (28K+ per run). */
            {
                X86CPU *x86cpu = X86_CPU(cpu);
                CPUState *cs = CPU(x86cpu);
                cs->interrupt_request &= ~CPU_INTERRUPT_POLL;
            }
            cpu->halted = 1;
            return;
        }

        /* 3. Both SIPIs done.  Match QEMU's TCG halt handling before deciding
         * whether to send the context remote.  POLL is a local APIC polling
         * hint; it must be consumed on the master-side LAPIC first so it can
         * turn into a real interrupt_request bit if an IRQ is pending. */
        if (cpu->halted) {
            X86CPU *x86cpu = X86_CPU(cpu);
            CPUState *cs = CPU(x86cpu);

            if (cs->interrupt_request & CPU_INTERRUPT_POLL) {
                qemu_mutex_lock_iothread();
                apic_poll_irq(x86cpu->apic_state);
                cs->interrupt_request &= ~CPU_INTERRUPT_POLL;
                qemu_mutex_unlock_iothread();
            }

            if (!cpu_has_work(cpu)) {
                return;
            }

            /*
             * HARD/POLL wakeups depend on the master-side LAPIC/PIC device
             * state to pick the actual interrupt vector.  The remote TCG
             * context only carries CPU architectural state, so dispatching a
             * freshly-woken halted AP directly to a slave loses that vector and
             * replays the same HLT.  Consume the wake locally, then let later
             * runnable compute bursts go remote.
             */
            wavevm_tcg_run_local_wake(cpu, x86cpu, ci);
            /* The bounded local re-entry can finish at HLT after consuming
             * the APIC event.  That is a valid clean handoff point, not a
             * reason to suppress the following remote execution slice. */
            if (cs->interrupt_request &
                (CPU_INTERRUPT_HARD | CPU_INTERRUPT_POLL)) {
                return;
            }
        }

        /*
         * A runnable TCG AP may also carry a pending master-side HARD/POLL.
         * Do not serialize those bits to a slave; the slave cannot derive the
         * interrupt vector without the master's local APIC/PIC device state.
         */
        {
            X86CPU *x86cpu = X86_CPU(cpu);
            CPUState *cs = CPU(x86cpu);

            if (cs->interrupt_request & CPU_INTERRUPT_POLL) {
                qemu_mutex_lock_iothread();
                apic_poll_irq(x86cpu->apic_state);
                cs->interrupt_request &= ~CPU_INTERRUPT_POLL;
                qemu_mutex_unlock_iothread();
            }
            if (cs->interrupt_request & CPU_INTERRUPT_HARD) {
                wavevm_tcg_run_local_wake(cpu, x86cpu, ci);
                if (cs->interrupt_request &
                    (CPU_INTERRUPT_HARD | CPU_INTERRUPT_POLL)) {
                    return;
                }
            }
        }
    }

    /* Log first remote dispatches after SIPI wake */
    {
        static int remote_dbg[MAX_VCPUS] = {0};
        int ci = cpu->cpu_index < MAX_VCPUS ? cpu->cpu_index : 0;
        if (remote_dbg[ci] < 10) {
            if (kvm_enabled()) {
                struct kvm_regs dbg_kr;
                struct kvm_sregs dbg_sr;
                ioctl(cpu->kvm_fd, KVM_GET_REGS, &dbg_kr);
                ioctl(cpu->kvm_fd, KVM_GET_SREGS, &dbg_sr);
                fprintf(stderr, "[WVM-REMOTE] cpu=%d dispatch #%d rip=0x%llx cs=0x%x "
                        "cr0=0x%llx efer=0x%llx rsp=0x%llx\n",
                        cpu->cpu_index, remote_dbg[ci],
                        (unsigned long long)dbg_kr.rip,
                        dbg_sr.cs.selector,
                        (unsigned long long)dbg_sr.cr0,
                        (unsigned long long)dbg_sr.efer,
                        (unsigned long long)dbg_kr.rsp);
            } else {
                X86CPU *x86cpu = X86_CPU(cpu);
                CPUX86State *env = &x86cpu->env;
                fprintf(stderr, "[WVM-REMOTE] cpu=%d dispatch #%d rip=0x%lx cs=0x%x "
                        "cr0=0x%lx efer=0x%lx rsp=0x%lx\n",
                        cpu->cpu_index, remote_dbg[ci],
                        (unsigned long)env->eip,
                        (unsigned)env->segs[R_CS].selector,
                        (unsigned long)env->cr[0],
                        (unsigned long)env->efer,
                        (unsigned long)env->regs[R_ESP]);
            }
            remote_dbg[ci]++;
        }
    }

    int vcpu_sock = g_vcpu_socks[cpu->cpu_index];
    if (!g_wvm_debug_once) {
        g_wvm_debug_once = 1;
        fprintf(stderr, "[WVM-DBG] cpu=%d kvm_enabled=%d vcpu_sock=%d split=%d\n",
                cpu->cpu_index, kvm_enabled(), vcpu_sock, g_wvm_local_split);
    }
    
    // 如果没有 socket (说明是 Kernel Mode), 走 IOCTL 路径
    if (vcpu_sock < 0) { 
        WaveVMAccelState *s = WAVEVM_ACCEL(current_machine->accelerator);
        if (s->mode != WVM_MODE_KERNEL) {
            g_usleep(1000); // 异常状态，非 Kernel 且无 Socket
            return;
        }

        // 1. 准备请求结构体
        struct wvm_ipc_cpu_run_req req;
        struct wvm_ipc_cpu_run_ack ack;
        memset(&req, 0, sizeof(req));
        
        req.slave_id = WVM_NODE_AUTO_ROUTE;
        req.vcpu_index = cpu->cpu_index;

        // 2. 序列化 CPU 状态
        if (kvm_enabled()) {
            struct kvm_regs kregs;
            struct kvm_sregs ksregs;
            cpu_synchronize_state(cpu);
            ioctl(cpu->kvm_fd, KVM_GET_REGS, &kregs);
            ioctl(cpu->kvm_fd, KVM_GET_SREGS, &ksregs);

            req.mode_tcg = 0;
            req.ctx.kvm.rax = kregs.rax; req.ctx.kvm.rbx = kregs.rbx; req.ctx.kvm.rcx = kregs.rcx;
            req.ctx.kvm.rdx = kregs.rdx; req.ctx.kvm.rsi = kregs.rsi; req.ctx.kvm.rdi = kregs.rdi;
            req.ctx.kvm.rsp = kregs.rsp; req.ctx.kvm.rbp = kregs.rbp;
            req.ctx.kvm.r8  = kregs.r8;  req.ctx.kvm.r9  = kregs.r9;  req.ctx.kvm.r10 = kregs.r10;
            req.ctx.kvm.r11 = kregs.r11; req.ctx.kvm.r12 = kregs.r12; req.ctx.kvm.r13 = kregs.r13;
            req.ctx.kvm.r14 = kregs.r14; req.ctx.kvm.r15 = kregs.r15;
            req.ctx.kvm.rip = kregs.rip; req.ctx.kvm.rflags = kregs.rflags;
            memcpy(req.ctx.kvm.sregs_data, &ksregs, sizeof(ksregs));
        } else {
            req.mode_tcg = 1;
            wvm_tcg_get_state(cpu, &req.ctx.tcg);
        }

        // 3. 陷入内核 (Trap into Kernel)
        // 这一步会阻塞，直到远程执行完毕并返回结果
        {
            static int dbg_kp_cnt = 0;
            if (dbg_kp_cnt < 20) {
                fprintf(stderr, "[WVM-DBG] kernel path cpu=%d target=%u mode_tcg=%u "
                        "rip=%#llx\n",
                        cpu->cpu_index, req.slave_id, req.mode_tcg,
                        (unsigned long long)req.ctx.kvm.rip);
                dbg_kp_cnt++;
            }
        }
        int ret = ioctl(s->dev_fd, IOCTL_WVM_REMOTE_RUN, &req);

        if (ret < 0) {
            static int dbg_fail_cnt = 0;
            if (dbg_fail_cnt < 10) {
                fprintf(stderr, "[WVM-DBG] IOCTL_WVM_REMOTE_RUN failed cpu=%d: %s\n",
                        cpu->cpu_index, strerror(errno));
                dbg_fail_cnt++;
            }
            return;
        }

        // 4. 反序列化结果 (直接复用 req 的内存空间读取 ack，或者使用 memcpy)
        // 注意：内核将 Ack 数据回写到了 req 指针所在的内存
        memcpy(&ack, &req, sizeof(ack)); 

        if (ack.mode_tcg) {
            wvm_tcg_set_state(cpu, &ack.ctx.tcg);
            cpu->halted = ack.ctx.tcg.halted ? 1 : 0;
        } else {
            struct kvm_regs kregs;
            struct kvm_sregs ksregs;
            wvm_kvm_context_t *kctx = &ack.ctx.kvm;

            kregs.rax = kctx->rax; kregs.rbx = kctx->rbx; kregs.rcx = kctx->rcx; 
            kregs.rdx = kctx->rdx; kregs.rsi = kctx->rsi; kregs.rdi = kctx->rdi;
            kregs.rsp = kctx->rsp; kregs.rbp = kctx->rbp;
            kregs.r8 = kctx->r8;   kregs.r9 = kctx->r9;   kregs.r10 = kctx->r10; 
            kregs.r11 = kctx->r11; kregs.r12 = kctx->r12; kregs.r13 = kctx->r13;
            kregs.r14 = kctx->r14; kregs.r15 = kctx->r15;
            kregs.rip = kctx->rip; kregs.rflags = kctx->rflags;
            
            ioctl(cpu->kvm_fd, KVM_GET_SREGS, &ksregs);
            wavevm_apply_arch_sregs(&ksregs, (const struct kvm_sregs *)kctx->sregs_data);
            ioctl(cpu->kvm_fd, KVM_SET_SREGS, &ksregs);
            ioctl(cpu->kvm_fd, KVM_SET_REGS, &kregs);
            
            struct kvm_run *run = cpu->kvm_run;
            run->exit_reason = kctx->exit_reason;

            if (kctx->exit_reason == KVM_EXIT_IO) {
                uint16_t port = kctx->io.port;
                int is_write = (kctx->io.direction == KVM_EXIT_IO_OUT);
                uint8_t io_buf[64];
                size_t io_bytes = (size_t)kctx->io.size * kctx->io.count;
                if (io_bytes > sizeof(kctx->io.data)) io_bytes = sizeof(kctx->io.data);
                if (io_bytes > sizeof(io_buf)) io_bytes = sizeof(io_buf);
                if (is_write) {
                    memcpy(io_buf, kctx->io.data, io_bytes);
                }
                run->io.direction = kctx->io.direction;
                run->io.size      = kctx->io.size;
                run->io.port      = kctx->io.port;
                run->io.count     = kctx->io.count;
                if (kctx->io.size != 1 && kctx->io.size != 2 &&
                    kctx->io.size != 4 && kctx->io.size != 8) { return; }
                if (kctx->io.count == 0 || kctx->io.count > 8) { return; }
                qemu_mutex_lock_iothread();
                for (uint32_t i = 0; i < kctx->io.count; i++) {
                    address_space_rw(&address_space_io, port, MEMTXATTRS_UNSPECIFIED,
                                     io_buf + i * kctx->io.size, kctx->io.size, is_write);
                }
                qemu_mutex_unlock_iothread();
                if (!is_write) {
                    memcpy(kctx->io.data, io_buf, io_bytes);
                    if (run->io.data_offset >= sizeof(struct kvm_run) &&
                        run->io.data_offset + io_bytes <= 12288) {
                        memcpy((uint8_t *)run + run->io.data_offset, io_buf, io_bytes);
                    }
                }
            } 
            else if (kctx->exit_reason == KVM_EXIT_MMIO) {
                uint8_t mmio_buf[8];
                hwaddr addr = kctx->mmio.phys_addr;
                int mmio_wr = kctx->mmio.is_write;
                unsigned len = kctx->mmio.len;
                if (len != 1 && len != 2 && len != 4 && len != 8) { return; }
                if (mmio_wr) {
                    memcpy(mmio_buf, kctx->mmio.data, len);
                }
                qemu_mutex_lock_iothread();
                address_space_rw(&address_space_memory, addr, MEMTXATTRS_UNSPECIFIED,
                                 mmio_buf, len, mmio_wr);
                qemu_mutex_unlock_iothread();
                if (!mmio_wr) {
                    memcpy(kctx->mmio.data, mmio_buf, len);
                    memcpy(run->mmio.data, mmio_buf, len);
                }
                run->mmio.phys_addr = addr;
                run->mmio.len = len;
                run->mmio.is_write = mmio_wr;
            }
            else if (kctx->exit_reason == KVM_EXIT_MMIO) {
                run->mmio.phys_addr = kctx->mmio.phys_addr;
                run->mmio.len       = kctx->mmio.len;
                run->mmio.is_write  = kctx->mmio.is_write;
                memcpy(run->mmio.data, kctx->mmio.data, 8);
                if (!wavevm_valid_mmio_exit(run)) {
                    return;
                }
                qemu_mutex_lock_iothread();
                wavevm_handle_mmio(cpu);
                qemu_mutex_unlock_iothread();
            }
        }
        return; 
    }

    // 准备发送缓冲区
    struct wvm_ipc_header_t ipc_hdr = {
        .type = WVM_IPC_TYPE_CPU_RUN,
        .len = sizeof(struct wvm_ipc_cpu_run_req),
    };
    struct wvm_ipc_cpu_run_req req;
    struct wvm_ipc_cpu_run_ack ack;
    memset(&req, 0, sizeof(req));

    req.slave_id = WVM_NODE_AUTO_ROUTE;
    req.vcpu_index = cpu->cpu_index;

    // 1. 序列化 CPU 状态 (Serialization)
    if (kvm_enabled()) {
        struct kvm_regs kregs;
        struct kvm_sregs ksregs;
        cpu_synchronize_state(cpu);
        ioctl(cpu->kvm_fd, KVM_GET_REGS, &kregs);
        ioctl(cpu->kvm_fd, KVM_GET_SREGS, &ksregs);

        /* [DIAG] Dump first 16 bytes at trampoline GPA + full regs for AP */
        if (cpu->cpu_index > 0) {
            static int memdump_count[MAX_VCPUS] = {0};
            int ci = cpu->cpu_index < MAX_VCPUS ? cpu->cpu_index : 0;
            if (memdump_count[ci] < 5) {
                uint8_t mem16[16];
                hwaddr gpa = (hwaddr)ksregs.cs.base + kregs.rip;
                cpu_physical_memory_read(gpa, mem16, 16);
                fprintf(stderr, "[MASTER-MEM] cpu=%d GPA=0x%llx bytes:"
                        " %02x %02x %02x %02x %02x %02x %02x %02x"
                        " %02x %02x %02x %02x %02x %02x %02x %02x\n",
                        cpu->cpu_index, (unsigned long long)gpa,
                        mem16[0],mem16[1],mem16[2],mem16[3],
                        mem16[4],mem16[5],mem16[6],mem16[7],
                        mem16[8],mem16[9],mem16[10],mem16[11],
                        mem16[12],mem16[13],mem16[14],mem16[15]);
                fprintf(stderr, "[MASTER-REGS] cpu=%d rip=0x%llx cs=0x%x cs_base=0x%llx "
                        "cr0=0x%llx efer=0x%llx rdx=0x%llx rsp=0x%llx\n",
                        cpu->cpu_index,
                        (unsigned long long)kregs.rip,
                        ksregs.cs.selector,
                        (unsigned long long)ksregs.cs.base,
                        (unsigned long long)ksregs.cr0,
                        (unsigned long long)ksregs.efer,
                        (unsigned long long)kregs.rdx,
                        (unsigned long long)kregs.rsp);
                memdump_count[ci]++;
            }
        }

        req.mode_tcg = 0;

        wvm_kvm_context_t *kctx = &req.ctx.kvm;
        kctx->rax = kregs.rax; kctx->rbx = kregs.rbx; kctx->rcx = kregs.rcx;
        kctx->rdx = kregs.rdx; kctx->rsi = kregs.rsi; kctx->rdi = kregs.rdi;
        kctx->rsp = kregs.rsp; kctx->rbp = kregs.rbp;
        kctx->r8  = kregs.r8;  kctx->r9  = kregs.r9;  kctx->r10 = kregs.r10;
        kctx->r11 = kregs.r11; kctx->r12 = kregs.r12; kctx->r13 = kregs.r13;
        kctx->r14 = kregs.r14; kctx->r15 = kregs.r15;
        kctx->rip = kregs.rip; kctx->rflags = kregs.rflags;
        memcpy(kctx->sregs_data, &ksregs, sizeof(ksregs));
        {
            struct kvm_lapic_state klapic;
            if (ioctl(cpu->kvm_fd, KVM_GET_LAPIC, &klapic) >= 0) {
                memcpy(kctx->lapic_data, &klapic, sizeof(klapic));
                kctx->lapic_valid = 1;
            } else {
                kctx->lapic_valid = 0;
            }
        }
        {
            struct kvm_vcpu_events kev;
            if (ioctl(cpu->kvm_fd, KVM_GET_VCPU_EVENTS, &kev) >= 0) {
                memcpy(kctx->vcpu_events_data, &kev, sizeof(kev));
                kctx->vcpu_events_valid = 1;
            } else {
                kctx->vcpu_events_valid = 0;
            }
        }
        {
            struct kvm_fpu fpu;
            if (ioctl(cpu->kvm_fd, KVM_GET_FPU, &fpu) >= 0) {
                memcpy(kctx->fpu_data, &fpu, sizeof(fpu));
                kctx->fpu_valid = 1;
            } else {
                kctx->fpu_valid = 0;
            }
        }
        {
            struct kvm_xcrs xcrs;
            if (ioctl(cpu->kvm_fd, KVM_GET_XCRS, &xcrs) >= 0) {
                memcpy(kctx->xcrs_data, &xcrs, sizeof(xcrs));
                kctx->xcrs_valid = 1;
            } else {
                kctx->xcrs_valid = 0;
            }
        }
        {
            struct kvm_mp_state mp;
            if (ioctl(cpu->kvm_fd, KVM_GET_MP_STATE, &mp) >= 0) {
                kctx->mp_state = mp.mp_state;
                kctx->mp_state_valid = 1;
            } else {
                kctx->mp_state_valid = 0;
            }
        }

        /* [V30 TSC 同步] 读取 master 本地 TSC 并携带到 slave，
         * slave 执行完后会把更新后的 TSC 带回来。
         * 避免 master/slave 之间 TSC 不同步导致 kvmclock 算出负 delta。 */
        {
            struct {
                struct kvm_msrs info;
                struct kvm_msr_entry entries[3];
            } msr_data;
            memset(&msr_data, 0, sizeof(msr_data));
            msr_data.info.nmsrs = 3;
            msr_data.entries[0].index = 0x10;       /* MSR_IA32_TSC */
            msr_data.entries[1].index = 0x6e0;      /* MSR_IA32_TSCDEADLINE */
            msr_data.entries[2].index = 0xC0000102;  /* MSR_KERNEL_GS_BASE */
            if (ioctl(cpu->kvm_fd, KVM_GET_MSRS, &msr_data) >= 0) {
                kctx->tsc_value = msr_data.entries[0].data;
                kctx->tsc_deadline = msr_data.entries[1].data;
                kctx->kernel_gs_base = msr_data.entries[2].data;
                kctx->tsc_valid = 1;
            } else {
                kctx->tsc_valid = 0;
            }
        }

        /* [FIX] 携带上一轮 IO/MMIO 处理结果：
         * 若上次远程执行触发了 IO IN 或 MMIO READ，Master 已在本地
         * wavevm_handle_io/mmio 中将设备读结果填入 run->io.data / mmio.data。
         * Slave 需要这些数据在下次 KVM_RUN 时完成未决的 IO 指令。 */
        struct kvm_run *run = cpu->kvm_run;
        kctx->exit_reason = run->exit_reason;
        if (run->exit_reason == KVM_EXIT_IO) {
            kctx->io.direction = run->io.direction;
            kctx->io.size      = run->io.size;
            kctx->io.port      = run->io.port;
            kctx->io.count     = run->io.count;
            if (run->io.direction == KVM_EXIT_IO_IN) {
                size_t io_bytes = (size_t)run->io.size * run->io.count;
                if (io_bytes > sizeof(kctx->io.data)) io_bytes = sizeof(kctx->io.data);
                memcpy(kctx->io.data, (uint8_t *)run + run->io.data_offset, io_bytes);
                {
                    static int io_send_dbg[MAX_VCPUS] = {0};
                    int ci = cpu->cpu_index < MAX_VCPUS ? cpu->cpu_index : 0;
                    if (io_send_dbg[ci] < 40) {
                        fprintf(stderr,
                                "[WVM-IO-SEND] cpu=%d port=0x%x dir=%u size=%u count=%u data0=0x%02x\n",
                                cpu->cpu_index,
                                kctx->io.port,
                                kctx->io.direction,
                                kctx->io.size,
                                kctx->io.count,
                                kctx->io.data[0]);
                        io_send_dbg[ci]++;
                    }
                }
            }
        } else if (run->exit_reason == KVM_EXIT_MMIO) {
            kctx->mmio.phys_addr = run->mmio.phys_addr;
            kctx->mmio.len       = run->mmio.len;
            kctx->mmio.is_write  = run->mmio.is_write;
            if (!run->mmio.is_write) {
                memcpy(kctx->mmio.data, run->mmio.data, 8);
            }
        }
    } else {
        req.mode_tcg = 1;
        wvm_tcg_get_state(cpu, &req.ctx.tcg);
    }

    {
        static int send_dbg[MAX_VCPUS] = {0};
        int ci = cpu->cpu_index < MAX_VCPUS ? cpu->cpu_index : 0;
        if (!req.mode_tcg && send_dbg[ci] < 20) {
            fprintf(stderr,
                    "[WVM-SEND] cpu=%d rip=0x%llx rax=0x%llx rdx=0x%llx mp_valid=%u mp=%u lapic=%u vcpu_events=%u tsc=%u\n",
                    cpu->cpu_index,
                    (unsigned long long)req.ctx.kvm.rip,
                    (unsigned long long)req.ctx.kvm.rax,
                    (unsigned long long)req.ctx.kvm.rdx,
                    req.ctx.kvm.mp_state_valid,
                    req.ctx.kvm.mp_state,
                    req.ctx.kvm.lapic_valid,
                    req.ctx.kvm.vcpu_events_valid,
                    req.ctx.kvm.tsc_valid);
            send_dbg[ci]++;
        }
    }

    // 2. IPC 发送 (wvm_ipc_header + wvm_ipc_cpu_run_req)
    if (write_full(vcpu_sock, &ipc_hdr, sizeof(ipc_hdr)) < 0) {
        return;
    }
    if (write_full(vcpu_sock, &req, sizeof(req)) < 0) {
        return;
    }

    // 3. IPC 接收 ACK (阻塞本线程，不影响其他 vCPU)
    if (read_full(vcpu_sock, &ack, sizeof(ack)) < 0) {
        return;
    }
    if (ack.status < 0) {
        return;
    }

    // 4. 反序列化 CPU 状态
    if (ack.mode_tcg) {
        wvm_tcg_set_state(cpu, &ack.ctx.tcg);
        cpu->halted = ack.ctx.tcg.halted ? 1 : 0;
    } else {
        struct kvm_regs kregs;
        struct kvm_sregs ksregs;
        wvm_kvm_context_t *kctx = &ack.ctx.kvm;

        /* Log remote dispatch results for debugging AP SMP bringup */
        {
            static int ack_dbg[MAX_VCPUS] = {0};
            int ci = cpu->cpu_index < MAX_VCPUS ? cpu->cpu_index : 0;
            if (ack_dbg[ci] < 30) {
                fprintf(stderr, "[WVM-ACK] cpu=%d ack#%d exit=%d rip=0x%llx cs_sel=0x%x "
                        "cr0=0x%llx status=%d",
                        cpu->cpu_index, ack_dbg[ci], kctx->exit_reason,
                        (unsigned long long)kctx->rip,
                        ((struct kvm_sregs *)kctx->sregs_data)->cs.selector,
                        (unsigned long long)((struct kvm_sregs *)kctx->sregs_data)->cr0,
                        ack.status);
                if (kctx->exit_reason == 2 /* KVM_EXIT_IO */) {
                    fprintf(stderr, " io_port=0x%x io_dir=%d io_sz=%d",
                            kctx->io.port, kctx->io.direction, kctx->io.size);
                } else if (kctx->exit_reason == 6 /* KVM_EXIT_MMIO */) {
                    fprintf(stderr, " mmio_addr=0x%llx mmio_wr=%d mmio_len=%d",
                            (unsigned long long)kctx->mmio.phys_addr,
                            kctx->mmio.is_write, kctx->mmio.len);
                }
                fprintf(stderr, "\n");
                ack_dbg[ci]++;
            }
        }
        if (kctx->exit_reason == KVM_EXIT_HLT) {
            static int hlt_ack_dbg[MAX_VCPUS] = {0};
            int ci = cpu->cpu_index < MAX_VCPUS ? cpu->cpu_index : 0;
            if (hlt_ack_dbg[ci] < 20) {
                const struct kvm_vcpu_events *kev =
                    (const struct kvm_vcpu_events *)kctx->vcpu_events_data;
                fprintf(stderr,
                        "[WVM-HLT-LAPIC] cpu=%d lapic=%u mp_valid=%u mp=%u vcpu_events=%u int_inj=%u int_nr=%u int_soft=%u int_shadow=%u exc_pend=%u nmi_pend=%u",
                        cpu->cpu_index,
                        kctx->lapic_valid,
                        kctx->mp_state_valid,
                        kctx->mp_state_valid ? kctx->mp_state : 0xffffffffu,
                        kctx->vcpu_events_valid,
                        kctx->vcpu_events_valid ? kev->interrupt.injected : 0,
                        kctx->vcpu_events_valid ? kev->interrupt.nr : 0,
                        kctx->vcpu_events_valid ? kev->interrupt.soft : 0,
                        kctx->vcpu_events_valid ? kev->interrupt.shadow : 0,
                        kctx->vcpu_events_valid ? kev->exception.pending : 0,
                        kctx->vcpu_events_valid ? kev->nmi.pending : 0);
                if (kctx->lapic_valid) {
                    const struct kvm_lapic_state *lapic =
                        (const struct kvm_lapic_state *)kctx->lapic_data;
                    fprintf(stderr,
                            " lvt=0x%08x init=0x%08x cur=0x%08x div=0x%08x svr=0x%08x",
                            wavevm_lapic_reg32(lapic, 0x320),
                            wavevm_lapic_reg32(lapic, 0x380),
                            wavevm_lapic_reg32(lapic, 0x390),
                            wavevm_lapic_reg32(lapic, 0x3e0),
                            wavevm_lapic_reg32(lapic, 0x0f0));
                }
                fprintf(stderr, "\n");
                hlt_ack_dbg[ci]++;
            }
        }

        kregs.rax = kctx->rax; kregs.rbx = kctx->rbx; kregs.rcx = kctx->rcx;
        kregs.rdx = kctx->rdx; kregs.rsi = kctx->rsi; kregs.rdi = kctx->rdi;
        kregs.rsp = kctx->rsp; kregs.rbp = kctx->rbp;
        kregs.r8 = kctx->r8;   kregs.r9 = kctx->r9;   kregs.r10 = kctx->r10;
        kregs.r11 = kctx->r11; kregs.r12 = kctx->r12; kregs.r13 = kctx->r13;
        kregs.r14 = kctx->r14; kregs.r15 = kctx->r15;
        kregs.rip = kctx->rip; kregs.rflags = kctx->rflags;
        if (kctx->lapic_valid) {
            struct kvm_lapic_state klapic;
            memcpy(&klapic, kctx->lapic_data, sizeof(klapic));
            ioctl(cpu->kvm_fd, KVM_SET_LAPIC, &klapic);
        }
        if (kctx->vcpu_events_valid) {
            struct kvm_vcpu_events kev;
            memcpy(&kev, kctx->vcpu_events_data, sizeof(kev));
            ioctl(cpu->kvm_fd, KVM_SET_VCPU_EVENTS, &kev);
        }
        if (kctx->fpu_valid) {
            struct kvm_fpu fpu;
            memcpy(&fpu, kctx->fpu_data, sizeof(fpu));
            ioctl(cpu->kvm_fd, KVM_SET_FPU, &fpu);
        }
        if (kctx->xcrs_valid) {
            struct kvm_xcrs xcrs;
            memcpy(&xcrs, kctx->xcrs_data, sizeof(xcrs));
            ioctl(cpu->kvm_fd, KVM_SET_XCRS, &xcrs);
        }
        if (kctx->mp_state_valid) {
            struct kvm_mp_state mp = { .mp_state = kctx->mp_state };
            ioctl(cpu->kvm_fd, KVM_SET_MP_STATE, &mp);
        }
        ioctl(cpu->kvm_fd, KVM_GET_SREGS, &ksregs);
        wavevm_apply_arch_sregs(&ksregs, (const struct kvm_sregs *)kctx->sregs_data);
        ioctl(cpu->kvm_fd, KVM_SET_SREGS, &ksregs);
        ioctl(cpu->kvm_fd, KVM_SET_REGS, &kregs);

        /* [V30 TSC 同步] 将 slave 端执行后的 TSC 值写回 master 本地 vCPU。
         * 没有这一步，master 本地 KVM 的 TSC 停留在 dispatch 前的旧值，
         * 而 kvmclock 用 TSC delta 计算 wall time，旧值导致负 delta
         * 产生 ~UINT64_MAX ns 的时间戳，Guest 内核 hang。 */
        if (kctx->tsc_valid) {
            struct {
                struct kvm_msrs info;
                struct kvm_msr_entry entries[3];
            } msr_data;
            memset(&msr_data, 0, sizeof(msr_data));
            msr_data.info.nmsrs = 3;
            msr_data.entries[0].index = 0x10;       /* MSR_IA32_TSC */
            msr_data.entries[0].data  = kctx->tsc_value;
            msr_data.entries[1].index = 0x6e0;      /* MSR_IA32_TSCDEADLINE */
            msr_data.entries[1].data  = kctx->tsc_deadline;
            msr_data.entries[2].index = 0xC0000102;  /* MSR_KERNEL_GS_BASE */
            msr_data.entries[2].data  = kctx->kernel_gs_base;
            ioctl(cpu->kvm_fd, KVM_SET_MSRS, &msr_data);
        }

        // 5. Replay IO/MMIO
        struct kvm_run *run = cpu->kvm_run;
        run->exit_reason = kctx->exit_reason;

        if (kctx->exit_reason == KVM_EXIT_IO) {
            /* [FIX] Direct IO replay using kctx->io.data -- bypass run->io.data_offset
             * which is uninitialized for remote vCPUs (never did local KVM_RUN). */
            uint16_t port = kctx->io.port;
            int is_write = (kctx->io.direction == KVM_EXIT_IO_OUT);
            uint8_t io_buf[64];
            size_t io_bytes = (size_t)kctx->io.size * kctx->io.count;
            if (io_bytes > sizeof(kctx->io.data)) io_bytes = sizeof(kctx->io.data);
            if (io_bytes > sizeof(io_buf)) io_bytes = sizeof(io_buf);
            if (is_write) {
                memcpy(io_buf, kctx->io.data, io_bytes);
            }
            run->io.direction = kctx->io.direction;
            run->io.size      = kctx->io.size;
            run->io.port      = kctx->io.port;
            run->io.count     = kctx->io.count;
            if (kctx->io.size != 1 && kctx->io.size != 2 &&
                kctx->io.size != 4 && kctx->io.size != 8) { return; }
            if (kctx->io.count == 0 || kctx->io.count > 8) { return; }
            qemu_mutex_lock_iothread();
            for (uint32_t i = 0; i < kctx->io.count; i++) {
                address_space_rw(&address_space_io, port, MEMTXATTRS_UNSPECIFIED,
                                 io_buf + i * kctx->io.size, kctx->io.size, is_write);
            }
            qemu_mutex_unlock_iothread();
            /* IO IN: copy device response for next request serialization */
            if (!is_write) {
                static int io_replay_dbg[MAX_VCPUS] = {0};
                int ci = cpu->cpu_index < MAX_VCPUS ? cpu->cpu_index : 0;
                if (io_replay_dbg[ci] < 40) {
                    fprintf(stderr,
                            "[WVM-IO-REPLAY] cpu=%d port=0x%x size=%u count=%u data0=0x%02x\n",
                            cpu->cpu_index, port, kctx->io.size, kctx->io.count,
                            io_buf[0]);
                    io_replay_dbg[ci]++;
                }
                memcpy(kctx->io.data, io_buf, io_bytes);
                if (run->io.data_offset >= sizeof(struct kvm_run) &&
                    run->io.data_offset + io_bytes <= 12288) {
                    memcpy((uint8_t *)run + run->io.data_offset, io_buf, io_bytes);
                }
            }
        } 
        else if (kctx->exit_reason == KVM_EXIT_MMIO) {
            uint8_t mmio_buf[8];
            hwaddr addr = kctx->mmio.phys_addr;
            int mmio_wr = kctx->mmio.is_write;
            unsigned len = kctx->mmio.len;
            bool lapic_mmio = (addr >= 0xfee00000ULL && addr < 0xfee01000ULL);
            if (len != 1 && len != 2 && len != 4 && len != 8) { return; }
            if (mmio_wr) {
                memcpy(mmio_buf, kctx->mmio.data, len);
            }
            if (lapic_mmio) {
                static int lapic_replay_dbg[MAX_VCPUS];
                int ci = cpu->cpu_index < MAX_VCPUS ? cpu->cpu_index : 0;
                if (lapic_replay_dbg[ci] < 80) {
                    fprintf(stderr,
                            "[WVM-MMIO-REPLAY] cpu=%d pa=0x%llx wr=%d len=%u data=0x",
                            cpu->cpu_index,
                            (unsigned long long)addr,
                            mmio_wr, len);
                    for (int bi = (int)len - 1; bi >= 0; bi--) {
                        fprintf(stderr, "%02x", kctx->mmio.data[bi]);
                    }
                    fprintf(stderr, "\n");
                    lapic_replay_dbg[ci]++;
                }
            }
            qemu_mutex_lock_iothread();
            address_space_rw(&address_space_memory, addr, MEMTXATTRS_UNSPECIFIED,
                             mmio_buf, len, mmio_wr);
            qemu_mutex_unlock_iothread();
            if (!mmio_wr) {
                memcpy(kctx->mmio.data, mmio_buf, len);
                memcpy(run->mmio.data, mmio_buf, len);
            }
            run->mmio.phys_addr = addr;
            run->mmio.len = len;
            run->mmio.is_write = mmio_wr;
        }
        else if (kctx->exit_reason == WVM_EXIT_PREEMPT) {
            X86CPU *x86cpu = X86_CPU(cpu);
            cpu->halted = 0;
            cpu->exception_index = 0;
            x86cpu->env.mp_state = KVM_MP_STATE_RUNNABLE;
            {
                struct kvm_mp_state run_mp = { .mp_state = KVM_MP_STATE_RUNNABLE };
                ioctl(cpu->kvm_fd, KVM_SET_MP_STATE, &run_mp);
            }
        }
        /* [FIX] HLT: halt the vCPU so QEMU waits for timer interrupt.
         *
         * But if the slave already returned a pending injected interrupt
         * (for example, a No-IRQCHIP LAPIC timer synthesized into
         * vcpu_events.interrupt), forcing HALTED here would immediately
         * bury that wake source again. Keep the vCPU RUNNABLE in that case
         * so the next local KVM entry can consume the interrupt. */
        else if (kctx->exit_reason == KVM_EXIT_HLT) {
            int ci = cpu->cpu_index < MAX_VCPUS ? cpu->cpu_index : 0;
            bool keep_pending_irq = false;

            if (kctx->vcpu_events_valid) {
                const struct kvm_vcpu_events *kev =
                    (const struct kvm_vcpu_events *)kctx->vcpu_events_data;
                keep_pending_irq = kev->interrupt.injected || kev->interrupt.nr;
            }

            if (keep_pending_irq) {
                X86CPU *x86cpu = X86_CPU(cpu);
                cpu->halted = 0;
                cpu->exception_index = 0;
                x86cpu->env.mp_state = KVM_MP_STATE_RUNNABLE;
                if (ci < MAX_VCPUS) {
                    ap_hlt_pending_irq[ci] = 1;
                }
                {
                    struct kvm_mp_state run_mp = { .mp_state = KVM_MP_STATE_RUNNABLE };
                    ioctl(cpu->kvm_fd, KVM_SET_MP_STATE, &run_mp);
                }
            } else {
                if (ci < MAX_VCPUS) {
                    ap_hlt_pending_irq[ci] = 0;
                }
                cpu->halted = 1;
                cpu->exception_index = EXCP_HLT;
                {
                    struct kvm_mp_state halt_mp = { .mp_state = KVM_MP_STATE_HALTED };
                    ioctl(cpu->kvm_fd, KVM_SET_MP_STATE, &halt_mp);
                }
            }
        }
    }
}

/* 
 * [物理意图] 重新定义 vCPU 的“心脏跳动规律”。
 * [关键逻辑] 拦截标准的 KVM_RUN 循环，根据调度策略决定本轮指令是交给本地 KVM 还是进行远程上下文序列化。
 * [后果] 这是超级虚拟机的总节拍器。它保证了在异构算力环境下，vCPU 能够平滑地在本地与远程之间切换执行流。
 */
/*
 * WaveVM vCPU thread function -- aligned with standard QEMU MTTCG skeleton.
 *
 * Key difference from vanilla MTTCG: the scheduling branch that routes
 * vCPUs to either local cpu_exec() or remote wavevm_remote_exec() based
 * on g_wvm_local_split.  Everything else (iothread lock discipline,
 * cpu_exec_start/end, exit_request, qemu_wait_io_event every iteration)
 * follows the standard QEMU pattern so that vm_start()/resume_all_vcpus()
 * and the full VM lifecycle management work correctly.
 */
static void *wavevm_cpu_thread_fn(void *arg) {
    CPUState *cpu = arg;
    char *role = getenv("WVM_ROLE");
    bool is_slave = (role && strcmp(role, "SLAVE") == 0);

    rcu_register_thread();
    /*
     * Register TCG context for all vCPUs.  With mttcg_enabled=true
     * (set in wavevm_init_tcg_region_once), tcg_n_regions() returns
     * max_cpus so every thread gets its own code-gen region.
     * Even "remote" APs need this because they run SIPI trampolines
     * locally via cpu_exec() before being dispatched to slaves.
     */
    if (!kvm_enabled()) {
        tcg_register_thread();
    }

    qemu_mutex_lock_iothread();
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    cpu->can_do_io = 1;
    current_cpu = cpu;
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    /* Slave: hand off to dedicated slave loop (holds its own lock discipline) */
    if (is_slave) {
        qemu_mutex_unlock_iothread();
        wavevm_slave_vcpu_loop(cpu);
        goto out;
    }

    /* KVM bootstrap for this vCPU (if KVM is the underlying engine) */
    if (kvm_enabled()) {
        kvm_init_vcpu(cpu, &error_fatal);
        kvm_init_cpu_signals(cpu);
        cpu_synchronize_state(cpu);
    }

    /*
     * Trigger an exit from the first cpu_exec() call so we cycle through
     * qemu_wait_io_event() and properly synchronize with vm_start().
     */
    cpu->exit_request = 1;

    static int bsp_dbg_count = 0;
    static int bsp_cant_run_count = 0;
    /*
     * Main vCPU loop -- mirrors tcg_cpu_thread_fn (MTTCG) structure.
     * The iothread lock is held at the top of every iteration.
     */
    do {
        if (cpu_can_run(cpu)) {
            /* Remote vCPU: forward to Slave via RPC */
            if (ops.schedule_policy(cpu->cpu_index) == 1) {
                qemu_mutex_unlock_iothread();
                wavevm_remote_exec(cpu);
                qemu_mutex_lock_iothread();
                /* [FIX] If remote exec returned HLT, wait for timer interrupt.
                 * Release iothread lock so main loop can process PIT timers.
                 * PIT fires at ~18.2Hz; poll every 1ms for interrupt_request. */
                if (cpu->halted) {
                    /* Check if remote_exec signaled first-RUNNABLE for local trampoline */
                    if (cpu->exception_index == 0x10003) {
                        cpu->exception_index = 0;
                        cpu->halted = 0;
                        /* AP just became RUNNABLE after SIPI. Run the BIOS SMP
                         * trampoline LOCALLY so the AP can:
                         * - Increment the BSP's AP counter in shared memory
                         * - Park itself with HLT
                         * After this, ap_did_local_sipi is set so future
                         * dispatches go to the remote slave. */
                        fprintf(stderr, "[WVM-SIPI] cpu=%d doing LOCAL kvm_cpu_exec for SIPI trampoline\n",
                                cpu->cpu_index);
                        /* Sync QEMU's internal mp_state from KVM before exec.
                         * Without this, kvm_arch_put_registers would push stale
                         * UNINITIALIZED and overwrite KVM's RUNNABLE state. */
                        {
                            X86CPU *x86cpu = X86_CPU(cpu);
                            x86cpu->env.mp_state = KVM_MP_STATE_RUNNABLE;
                        }
                        cpu_synchronize_state(cpu);
                        /* Two-phase SIPI trampoline execution:
                         * Phase 1: kvm_cpu_exec runs partial trampoline, returns
                         *          EXCP_INTERRUPT (signal preemption). AP has NOT
                         *          completed yet — SMP counter not incremented.
                         * Phase 2: kvm_cpu_exec continues trampoline, AP increments
                         *          counter and HLTs. But KVM_RUN blocks forever on
                         *          HLT. Use timer_create(SIGEV_THREAD_ID) to send
                         *          SIGUSR1 (SIG_IPI) to THIS thread after 2s,
                         *          kicking KVM_RUN out. Then check mp_state. */
                        {
                            /* Phase 1 */
                            int sipi_r = kvm_cpu_exec(cpu);
                            struct kvm_mp_state mp = {0};
                            ioctl(cpu->kvm_fd, KVM_GET_MP_STATE, &mp);
                            fprintf(stderr, "[WVM-SIPI] cpu=%d phase1 ret=%d halted=%d kvm_mp=%d\n",
                                    cpu->cpu_index, sipi_r, cpu->halted, mp.mp_state);

                            if (!cpu->halted && mp.mp_state != KVM_MP_STATE_HALTED) {
                                /* Phase 2: create per-thread timer to kick us out */
                                timer_t sipi_timer;
                                struct sigevent sev;
                                memset(&sev, 0, sizeof(sev));
                                sev.sigev_notify = SIGEV_THREAD_ID;
                                sev.sigev_signo = SIGUSR1;
                                sev._sigev_un._tid = syscall(SYS_gettid);
                                if (timer_create(CLOCK_MONOTONIC, &sev, &sipi_timer) == 0) {
                                    struct itimerspec its = {0};
                                    its.it_value.tv_sec = 2;
                                    timer_settime(sipi_timer, 0, &its, NULL);

                                    fprintf(stderr, "[WVM-SIPI] cpu=%d phase2 entering kvm_cpu_exec with 2s timer\n",
                                            cpu->cpu_index);
                                    int sipi_r2 = kvm_cpu_exec(cpu);

                                    /* Disarm and delete timer */
                                    memset(&its, 0, sizeof(its));
                                    timer_settime(sipi_timer, 0, &its, NULL);
                                    timer_delete(sipi_timer);

                                    ioctl(cpu->kvm_fd, KVM_GET_MP_STATE, &mp);
                                    fprintf(stderr, "[WVM-SIPI] cpu=%d phase2 ret=%d halted=%d kvm_mp=%d\n",
                                            cpu->cpu_index, sipi_r2, cpu->halted, mp.mp_state);
                                } else {
                                    fprintf(stderr, "[WVM-SIPI] cpu=%d timer_create failed errno=%d\n",
                                            cpu->cpu_index, errno);
                                }
                            }

                            /* Park the AP */
                            if (!cpu->halted) {
                                if (mp.mp_state == KVM_MP_STATE_HALTED) {
                                    cpu->halted = 1;
                                    fprintf(stderr, "[WVM-SIPI] cpu=%d KVM HALTED, parking AP\n",
                                            cpu->cpu_index);
                                } else {
                                    /* Trampoline should be done by now (2s is plenty).
                                     * Force halt as last resort. */
                                    struct kvm_mp_state halt_mp = { .mp_state = KVM_MP_STATE_HALTED };
                                    ioctl(cpu->kvm_fd, KVM_SET_MP_STATE, &halt_mp);
                                    cpu->halted = 1;
                                    fprintf(stderr, "[WVM-SIPI] cpu=%d force-halted after phase2 (mp was %d)\n",
                                            cpu->cpu_index, mp.mp_state);
                                }
                            } else {
                                fprintf(stderr, "[WVM-SIPI] cpu=%d naturally halted\n", cpu->cpu_index);
                            }
                        }
                        /* Mark trampoline as done */
                        if (cpu->cpu_index < MAX_VCPUS) {
                            ap_did_local_sipi[cpu->cpu_index] = 1;
                        }
                        continue;
                    }
                    /* Check MP state: AP in SIPI-wait should stay halted
                     * until SIPI is delivered. */
                    if (kvm_enabled()) {
                        struct kvm_mp_state mp;
                        ioctl(cpu->kvm_fd, KVM_GET_MP_STATE, &mp);

                        if (mp.mp_state != KVM_MP_STATE_RUNNABLE &&
                            cpu->cpu_index < MAX_VCPUS &&
                            ap_did_local_sipi[cpu->cpu_index] &&
                            !ap_did_os_sipi[cpu->cpu_index]) {
                            /* AP is HALTED after BIOS trampoline, waiting for
                             * Linux's INIT+SIPI. KVM_GET_MP_STATE alone may not
                             * reliably process pending LAPIC events on all host
                             * kernels. Use real KVM_RUN: in HALTED state, KVM
                             * efficiently waits for interrupt (like HLT). When
                             * INIT+SIPI arrives, KVM processes it, transitions
                             * to RUNNABLE, and starts executing the OS trampoline.
                             * Use a timer to bound the wait. */
                            static int os_sipi_dbg = 0;
                            if (os_sipi_dbg < 10) {
                                fprintf(stderr, "[OS-SIPI] cpu=%d entering kvm_cpu_exec "
                                        "for OS SIPI (mp=%d)\n",
                                        cpu->cpu_index, mp.mp_state);
                                os_sipi_dbg++;
                            }

                            /* Sync QEMU mp_state from KVM before exec */
                            {
                                X86CPU *x86cpu = X86_CPU(cpu);
                                x86cpu->env.mp_state = mp.mp_state;
                            }

                            /* Create per-thread timer: 5s to receive INIT+SIPI
                             * and complete the Linux AP bringup. */
                            timer_t os_sipi_timer;
                            struct sigevent sev;
                            memset(&sev, 0, sizeof(sev));
                            sev.sigev_notify = SIGEV_THREAD_ID;
                            sev.sigev_signo = SIGUSR1;
                            sev._sigev_un._tid = syscall(SYS_gettid);

                            if (timer_create(CLOCK_MONOTONIC, &sev, &os_sipi_timer) == 0) {
                                struct itimerspec its = {0};
                                its.it_value.tv_sec = 5;
                                timer_settime(os_sipi_timer, 0, &its, NULL);

                                /* Run AP: KVM_RUN in HALTED state waits for
                                 * interrupt. INIT+SIPI wakes it, AP executes
                                 * Linux trampoline locally until HLT or timer. */
                                cpu->halted = 0;
                                int os_r = kvm_cpu_exec(cpu);

                                /* Disarm and delete timer */
                                memset(&its, 0, sizeof(its));
                                timer_settime(os_sipi_timer, 0, &its, NULL);
                                timer_delete(os_sipi_timer);

                                ioctl(cpu->kvm_fd, KVM_GET_MP_STATE, &mp);
                                if (os_sipi_dbg < 20) {
                                    fprintf(stderr, "[OS-SIPI] cpu=%d kvm_cpu_exec ret=%d "
                                            "halted=%d mp=%d\n",
                                            cpu->cpu_index, os_r,
                                            cpu->halted, mp.mp_state);
                                    os_sipi_dbg++;
                                }

                                if (mp.mp_state == KVM_MP_STATE_RUNNABLE) {
                                    /* AP woke up but didn't HLT yet — needs more
                                     * execution. Loop kvm_cpu_exec with timer. */
                                    struct itimerspec its2 = {0};
                                    its2.it_value.tv_sec = 5;
                                    if (timer_create(CLOCK_MONOTONIC, &sev, &os_sipi_timer) == 0) {
                                        timer_settime(os_sipi_timer, 0, &its2, NULL);
                                        int os_r2 = kvm_cpu_exec(cpu);
                                        memset(&its2, 0, sizeof(its2));
                                        timer_settime(os_sipi_timer, 0, &its2, NULL);
                                        timer_delete(os_sipi_timer);
                                        ioctl(cpu->kvm_fd, KVM_GET_MP_STATE, &mp);
                                        fprintf(stderr, "[OS-SIPI] cpu=%d phase2 ret=%d "
                                                "halted=%d mp=%d\n",
                                                cpu->cpu_index, os_r2,
                                                cpu->halted, mp.mp_state);
                                    }
                                }

                                /* Park the AP after OS trampoline */
                                if (mp.mp_state == KVM_MP_STATE_HALTED) {
                                    cpu->halted = 1;
                                    if (cpu->cpu_index < MAX_VCPUS)
                                        ap_did_os_sipi[cpu->cpu_index] = 1;
                                    fprintf(stderr, "[OS-SIPI] cpu=%d OS trampoline done, "
                                            "AP parked (HALTED). Ready for remote dispatch.\n",
                                            cpu->cpu_index);
                                } else if (mp.mp_state == KVM_MP_STATE_RUNNABLE) {
                                    /* AP is still running — dispatch remotely */
                                    cpu->halted = 0;
                                    if (cpu->cpu_index < MAX_VCPUS)
                                        ap_did_os_sipi[cpu->cpu_index] = 1;
                                    fprintf(stderr, "[OS-SIPI] cpu=%d OS trampoline RUNNABLE, "
                                            "switching to remote dispatch\n",
                                            cpu->cpu_index);
                                    {
                                        X86CPU *x86cpu = X86_CPU(cpu);
                                        x86cpu->env.mp_state = KVM_MP_STATE_RUNNABLE;
                                    }
                                    cpu_synchronize_state(cpu);
                                } else {
                                    /* Timer expired, no SIPI received */
                                    cpu->halted = 1;
                                    fprintf(stderr, "[OS-SIPI] cpu=%d timer expired, "
                                            "no OS SIPI (mp=%d)\n",
                                            cpu->cpu_index, mp.mp_state);
                                }
                            } else {
                                fprintf(stderr, "[OS-SIPI] cpu=%d timer_create failed "
                                        "errno=%d, fallback to poll\n",
                                        cpu->cpu_index, errno);
                                qemu_mutex_unlock_iothread();
                                g_usleep(10000);
                                qemu_mutex_lock_iothread();
                            }
                            continue;
                        }

                        if (mp.mp_state == KVM_MP_STATE_RUNNABLE) {
                            /* INIT+SIPI delivered! AP should now run the OS
                             * SMP trampoline. Unhalt for remote dispatch. */
                            cpu->halted = 0;
                            fprintf(stderr, "[WVM-WAKE] cpu=%d woken by OS SIPI "
                                    "(mp_state now RUNNABLE)\n", cpu->cpu_index);
                            {
                                X86CPU *x86cpu = X86_CPU(cpu);
                                x86cpu->env.mp_state = KVM_MP_STATE_RUNNABLE;
                            }
                            cpu_synchronize_state(cpu);
                            continue;
                        } else if (cpu->cpu_index < MAX_VCPUS &&
                                   ap_did_local_sipi[cpu->cpu_index] &&
                                   ap_did_os_sipi[cpu->cpu_index] &&
                                   mp.mp_state == KVM_MP_STATE_HALTED) {
                            /* Post-OS-SIPI idle APs should use QEMU's normal
                             * halted wakeup path. Spinning here on MP state
                             * can miss kick/interrupt driven wakeups and trap
                             * the AP in a HALT-WAKE loop forever. */
                            cpu->halted = 1;
                        } else {
                            /* Still waiting (non-KVM or pre-BIOS-SIPI) */
                            static int poll_dbg = 0;
                            if (poll_dbg < 20) {
                                fprintf(stderr, "[WVM-POLL] cpu=%d halted mp_state=%d "
                                        "(waiting for RUNNABLE)\n",
                                        cpu->cpu_index, mp.mp_state);
                                poll_dbg++;
                            }
                            qemu_mutex_unlock_iothread();
                            g_usleep(10000);
                            qemu_mutex_lock_iothread();
                            continue;
                        }
                    }
                    /* AP is halted (from remote HLT or local trampoline HLT).
                     * Stay parked until an external interrupt (IPI) wakes it.
                     * This is the normal AP parking state after BIOS SMP init. */
                    {
                    /* In TCG mode, also wake on SIPI/INIT (software APIC
                     * delivers these as interrupt_request bits, not via
                     * KVM MP state).  KVM path: only HARD|POLL needed
                     * (SIPI is handled by KVM MP state transitions above).
                     * TCG path: POLL is useless for parked APs — it just
                     * polls the APIC which has no pending IRQs for APs. */
                    uint32_t wake_mask = CPU_INTERRUPT_HARD;
                    if (kvm_enabled()) {
                        wake_mask |= CPU_INTERRUPT_POLL;
                    } else {
                        wake_mask |= CPU_INTERRUPT_SIPI | CPU_INTERRUPT_INIT;
                    }
                    if (cpu->interrupt_request & wake_mask) {
                        cpu->halted = 0;
                        fprintf(stderr, "[WVM-WAKE] cpu=%d woken by interrupt (irq=0x%x)\n",
                                cpu->cpu_index, cpu->interrupt_request);
                    } else {
                        /*
                         * No interrupt: stay halted, but still enter QEMU's
                         * normal wait path so queued run_on_cpu work and
                         * kicks are processed while the AP is parked.
                         */
                        qemu_wait_io_event(cpu);
                        continue;
                    }
                    }
                }
            }
            /* Local vCPU: 复用 QEMU 原生 kvm_cpu_exec()
             * 包含完整的 kvm_arch_pre_run (中断注入/APIC 同步)、
             * kvm_arch_post_run (状态回收)、kvm_arch_put_registers
             * (脏寄存器推送) 以及全部 exit reason 处理。
             */
            else if (kvm_enabled()) {
                if (bsp_dbg_count < 20) {
                    fprintf(stderr, "[BSP-DBG] cpu=%d iter=%d halted=%d stop=%d exit_req=%d run->exit=%d\n",
                            cpu->cpu_index, bsp_dbg_count, cpu->halted, cpu->stop,
                            cpu->exit_request, cpu->kvm_run ? cpu->kvm_run->exit_reason : -1);
                }
                int r = kvm_cpu_exec(cpu);
                if (bsp_dbg_count < 200) {
                    struct kvm_regs bsp_kr;
                    struct kvm_sregs bsp_sr;
                    ioctl(cpu->kvm_fd, KVM_GET_REGS, &bsp_kr);
                    ioctl(cpu->kvm_fd, KVM_GET_SREGS, &bsp_sr);
                    fprintf(stderr, "[BSP-DBG] cpu=%d iter=%d ret=%d rip=0x%llx cs=0x%x/0x%llx rfl=0x%llx rax=0x%llx halted=%d\n",
                            cpu->cpu_index, bsp_dbg_count, r,
                            (unsigned long long)bsp_kr.rip,
                            bsp_sr.cs.selector, (unsigned long long)bsp_sr.cs.base,
                            (unsigned long long)bsp_kr.rflags,
                            (unsigned long long)bsp_kr.rax,
                            cpu->halted);
                    bsp_dbg_count++;
                }
                if (r == EXCP_DEBUG) {
                    cpu_handle_guest_debug(cpu);
                }
            } else {
                /* TCG local execution */
                int r;
                qemu_mutex_unlock_iothread();
                cpu_exec_start(cpu);
                r = cpu_exec(cpu);
                cpu_exec_end(cpu);
                qemu_mutex_lock_iothread();

                switch (r) {
                case EXCP_DEBUG:
                    cpu_handle_guest_debug(cpu);
                    break;
                case EXCP_HALTED:
                    break;
                case EXCP_ATOMIC:
                    qemu_mutex_unlock_iothread();
                    cpu_exec_step_atomic(cpu);
                    qemu_mutex_lock_iothread();
                    break;
                default:
                    break;
                }
            }
        }

        qatomic_mb_set(&cpu->exit_request, 0);
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));

    if (kvm_enabled() && cpu->kvm_fd >= 0) {
        kvm_destroy_vcpu(cpu);
    }
    cpu_thread_signal_destroyed(cpu);
    qemu_mutex_unlock_iothread();
out:
    rcu_unregister_thread();
    return NULL;
}

/* 
 * [物理意图] 为每个逻辑核心开辟专属的“高速公路（Per-vCPU Socket Pool）”。
 * [关键逻辑] 在 vCPU 创建瞬间建立独立的 IPC 通道，彻底消除多核并发时的 Socket 锁竞争。
 * [后果] 实现了算力的线性扩展能力。这是 V30 支撑一亿核心的关键，保证了 vCPU 数量增加时通信延迟不上升。
 */
void wavevm_start_vcpu_thread(CPUState *cpu) {
    char thread_name[VCPU_THREAD_NAME_SIZE];
    WaveVMAccelState *s = WAVEVM_ACCEL(current_machine->accelerator);
    char *role = getenv("WVM_ROLE");

    static pthread_mutex_t g_init_lock = PTHREAD_MUTEX_INITIALIZER;

    if (!kvm_enabled()) {
        pthread_once(&g_wavevm_tcg_region_once, wavevm_init_tcg_region_once);
    }
    
    // 双重检查锁定 (Double-Checked Locking) 优化性能，或者直接加锁也行（毕竟只执行一次）
    if (!g_vcpu_socks) {
        pthread_mutex_lock(&g_init_lock);
        // 再次检查，防止在等待锁的过程中已被其他线程初始化
        if (!g_vcpu_socks) {
            g_configured_vcpus = (current_machine && current_machine->smp.cpus > 0) ?
                                 current_machine->smp.cpus : 1;
            
            g_vcpu_socks = g_malloc0(sizeof(int) * g_configured_vcpus);
            for (int i = 0; i < g_configured_vcpus; i++) {
                g_vcpu_socks[i] = -1;
            }
        }
        pthread_mutex_unlock(&g_init_lock);
    }

    if (s->mode == WVM_MODE_USER && !(role && strcmp(role, "SLAVE") == 0)) {
        // 动态边界检查
        if (cpu->cpu_index < g_configured_vcpus) {
            g_vcpu_socks[cpu->cpu_index] = connect_to_master_helper();
            if (g_vcpu_socks[cpu->cpu_index] < 0) {
                fprintf(stderr, "vCPU %d failed to connect master!\n", cpu->cpu_index);
                exit(1);
            }
        }
    } else {
        // Kernel 模式和 Slave 模式不使用此机制
        g_vcpu_socks[cpu->cpu_index] = -1; 
    }
    
    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(cpu->halt_cond);
    
    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/WVM", cpu->cpu_index);
    
    qemu_thread_create(cpu->thread, thread_name, wavevm_cpu_thread_fn, cpu, QEMU_THREAD_JOINABLE);
}
