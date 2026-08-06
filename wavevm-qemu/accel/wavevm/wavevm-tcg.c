
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "hw/i386/apic.h"
#include "hw/i386/apic_internal.h"
#include "../../../common_include/wavevm_protocol.h"

#if defined(TARGET_I386) || defined(TARGET_X86_64)

QEMU_BUILD_BUG_ON(sizeof(X86XSaveArea) > WVM_TCG_XSAVE_AREA_SIZE);
QEMU_BUILD_BUG_ON(MSR_MTRRcap_VCNT != WVM_TCG_MTRR_VAR_NB);

/*
 * Keep guest-visible/wakeup interrupt state in the remote TCG context.
 * Local execution-control bits such as EXITTB/DEBUG are meaningful only
 * to the current QEMU instance and are intentionally not serialized.
 */
#define WVM_TCG_INTERRUPT_SYNC_MASK \
    (CPU_INTERRUPT_HARD | CPU_INTERRUPT_HALT | \
     CPU_INTERRUPT_TGT_EXT_0 | CPU_INTERRUPT_TGT_EXT_1 | \
     CPU_INTERRUPT_TGT_EXT_2 | CPU_INTERRUPT_TGT_EXT_3 | \
     CPU_INTERRUPT_TGT_EXT_4 | CPU_INTERRUPT_TGT_INT_0 | \
     CPU_INTERRUPT_TGT_INT_1 | CPU_INTERRUPT_TGT_INT_2)

static void wvm_tcg_get_lapic_state(CPUState *cpu,
                                    wvm_tcg_lapic_state_t *dst)
{
    X86CPU *x86_cpu = X86_CPU(cpu);

    memset(dst, 0, sizeof(*dst));
    if (!x86_cpu->apic_state) {
        return;
    }

    APICCommonState *apic = APIC_COMMON(x86_cpu->apic_state);
    APICCommonClass *klass = APIC_COMMON_GET_CLASS(apic);

    if (klass->pre_save) {
        klass->pre_save(apic);
    }

    dst->valid = 1;
    dst->apicbase = apic->apicbase;
    dst->initial_apic_id = apic->initial_apic_id;
    dst->spurious_vec = apic->spurious_vec;
    memcpy(dst->isr, apic->isr, sizeof(dst->isr));
    memcpy(dst->tmr, apic->tmr, sizeof(dst->tmr));
    memcpy(dst->irr, apic->irr, sizeof(dst->irr));
    memcpy(dst->lvt, apic->lvt, sizeof(dst->lvt));
    dst->esr = apic->esr;
    memcpy(dst->icr, apic->icr, sizeof(dst->icr));
    dst->divide_conf = apic->divide_conf;
    dst->initial_count = apic->initial_count;
    dst->count_shift = apic->count_shift;
    dst->initial_count_load_time = apic->initial_count_load_time;
    dst->next_time = apic->next_time;
    dst->timer_expiry = apic->timer_expiry;
    dst->sipi_vector = apic->sipi_vector;
    dst->wait_for_sipi = apic->wait_for_sipi;
    dst->id = apic->id;
    dst->arb_id = apic->arb_id;
    dst->tpr = apic->tpr;
    dst->log_dest = apic->log_dest;
    dst->dest_mode = apic->dest_mode;
    dst->version = apic->version;
}

static void wvm_tcg_set_lapic_state(CPUState *cpu,
                                    const wvm_tcg_lapic_state_t *src)
{
    X86CPU *x86_cpu = X86_CPU(cpu);

    if (!src->valid || !x86_cpu->apic_state) {
        return;
    }

    APICCommonState *apic = APIC_COMMON(x86_cpu->apic_state);
    APICCommonClass *klass = APIC_COMMON_GET_CLASS(apic);

    apic->apicbase = src->apicbase;
    apic->initial_apic_id = src->initial_apic_id;
    apic->spurious_vec = src->spurious_vec;
    memcpy(apic->isr, src->isr, sizeof(apic->isr));
    memcpy(apic->tmr, src->tmr, sizeof(apic->tmr));
    memcpy(apic->irr, src->irr, sizeof(apic->irr));
    memcpy(apic->lvt, src->lvt, sizeof(apic->lvt));
    apic->esr = src->esr;
    memcpy(apic->icr, src->icr, sizeof(apic->icr));
    apic->divide_conf = src->divide_conf;
    apic->initial_count = src->initial_count;
    apic->count_shift = src->count_shift;
    apic->initial_count_load_time = src->initial_count_load_time;
    apic->next_time = src->next_time;
    apic->timer_expiry = src->timer_expiry;
    apic->sipi_vector = src->sipi_vector;
    apic->wait_for_sipi = src->wait_for_sipi;
    apic->id = src->id;
    apic->arb_id = src->arb_id;
    apic->tpr = src->tpr;
    apic->log_dest = src->log_dest;
    apic->dest_mode = src->dest_mode;
    apic->version = src->version;

    if (klass->post_load) {
        klass->post_load(apic);
    }
    apic_poll_irq(x86_cpu->apic_state);
}

// Export QEMU TCG state to network packet
void wvm_tcg_get_state(CPUState *cpu, wvm_tcg_context_t *ctx) {
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    memset(ctx, 0, sizeof(*ctx));

    // 1. General Registers
    memcpy(ctx->regs, env->regs, sizeof(ctx->regs));
    ctx->eip = env->eip;
    ctx->eflags = cpu_compute_eflags(env);

    // 2. Control Registers
    ctx->cr[0] = env->cr[0];
    ctx->cr[2] = env->cr[2];
    ctx->cr[3] = env->cr[3];
    ctx->cr[4] = env->cr[4];
    
    // 3. SSE/AVX Registers
    // Synchronize XMM0-XMM15 to prevent guest OS crash
    for (int i = 0; i < 16; i++) {
        // Accessing ZMMReg union safely
        // ZMM_Q(n) accesses the nth 64-bit part of the register
        ctx->xmm_regs[i*2]     = env->xmm_regs[i].ZMM_Q(0);
        ctx->xmm_regs[i*2 + 1] = env->xmm_regs[i].ZMM_Q(1);
    }
    ctx->mxcsr = env->mxcsr;

    ctx->exit_reason = cpu->exception_index;
    ctx->halted = cpu->halted ? 1 : 0;
    ctx->interrupt_request = cpu->interrupt_request & WVM_TCG_INTERRUPT_SYNC_MASK;
    ctx->_pad0 = 0;

    ctx->fs_base = env->segs[R_FS].base;
    ctx->gs_base = env->segs[R_GS].base;
    ctx->gdt_base = env->gdt.base;
    ctx->gdt_limit = env->gdt.limit;
    ctx->idt_base = env->idt.base;
    ctx->idt_limit = env->idt.limit;

    /* V31: Full segment register state — critical for real/protected mode */
    for (int i = 0; i < 6; i++) {
        ctx->segs[i].base     = env->segs[i].base;
        ctx->segs[i].limit    = env->segs[i].limit;
        ctx->segs[i].selector = env->segs[i].selector;
        ctx->segs[i].flags    = env->segs[i].flags;
    }
    ctx->ldt.base     = env->ldt.base;
    ctx->ldt.limit    = env->ldt.limit;
    ctx->ldt.selector = env->ldt.selector;
    ctx->ldt.flags    = env->ldt.flags;
    ctx->tr.base      = env->tr.base;
    ctx->tr.limit     = env->tr.limit;
    ctx->tr.selector  = env->tr.selector;
    ctx->tr.flags     = env->tr.flags;
    ctx->efer         = env->efer;
    ctx->star         = env->star;
    ctx->sysenter_cs  = env->sysenter_cs;
    ctx->sysenter_esp = env->sysenter_esp;
    ctx->sysenter_eip = env->sysenter_eip;
#ifdef TARGET_X86_64
    ctx->lstar        = env->lstar;
    ctx->cstar        = env->cstar;
    ctx->fmask        = env->fmask;
    ctx->kernelgsbase = env->kernelgsbase;
#else
    ctx->lstar = ctx->cstar = ctx->fmask = ctx->kernelgsbase = 0;
#endif
    ctx->pat           = env->pat;
    ctx->hflags        = env->hflags;
    ctx->hflags2       = env->hflags2;
    ctx->a20_mask      = env->a20_mask;
    ctx->mp_state      = env->mp_state;
    memcpy(ctx->dr, env->dr, sizeof(ctx->dr));
    ctx->vm_hsave      = env->vm_hsave;
    ctx->vm_vmcb       = env->vm_vmcb;
    ctx->tsc_offset    = env->tsc_offset;
    ctx->intercept     = env->intercept;
    ctx->nested_cr3    = env->nested_cr3;
    ctx->nested_pg_mode = env->nested_pg_mode;
    ctx->intercept_cr_read = env->intercept_cr_read;
    ctx->intercept_cr_write = env->intercept_cr_write;
    ctx->intercept_dr_read = env->intercept_dr_read;
    ctx->intercept_dr_write = env->intercept_dr_write;
    ctx->intercept_exceptions = env->intercept_exceptions;
    ctx->v_tpr         = env->v_tpr;
    memcpy(ctx->mtrr_fixed, env->mtrr_fixed, sizeof(ctx->mtrr_fixed));
    ctx->mtrr_deftype  = env->mtrr_deftype;
    for (int i = 0; i < WVM_TCG_MTRR_VAR_NB; i++) {
        ctx->mtrr_var[i].base = env->mtrr_var[i].base;
        ctx->mtrr_var[i].mask = env->mtrr_var[i].mask;
    }
    ctx->system_time_msr = env->system_time_msr;
    ctx->wall_clock_msr = env->wall_clock_msr;
    ctx->steal_time_msr = env->steal_time_msr;
    ctx->async_pf_en_msr = env->async_pf_en_msr;
    ctx->async_pf_int_msr = env->async_pf_int_msr;
    ctx->pv_eoi_en_msr = env->pv_eoi_en_msr;
    ctx->poll_control_msr = env->poll_control_msr;
    ctx->msr_bndcfgs   = env->msr_bndcfgs;
    ctx->exception_nr  = env->exception_nr;
    ctx->interrupt_injected = env->interrupt_injected;
    ctx->soft_interrupt = env->soft_interrupt;
    ctx->nmi_injected  = env->nmi_injected;
    ctx->nmi_pending   = env->nmi_pending;
    ctx->has_error_code = env->has_error_code;
    ctx->exception_pending = env->exception_pending;
    ctx->exception_injected = env->exception_injected;
    ctx->exception_has_payload = env->exception_has_payload;
    ctx->exception_payload = env->exception_payload;
    ctx->ins_len       = env->ins_len;
    ctx->sipi_vector   = env->sipi_vector;
    /*
     * old_exception/error_code/exception_is_int/exception_next_eip are QEMU
     * exception-delivery scratch state, not persistent architectural state.
     * Serializing an in-flight double-fault marker makes the receiving TCG
     * instance treat the next normal page fault as a triple fault.
     */
    ctx->old_exception = -1;
    ctx->smbase        = env->smbase;
    ctx->tsc           = env->tsc;
    ctx->tsc_adjust    = env->tsc_adjust;
    ctx->tsc_deadline  = env->tsc_deadline;
    ctx->tsc_aux       = env->tsc_aux;
    ctx->xcr0          = env->xcr0;
    ctx->msr_ia32_misc_enable = env->msr_ia32_misc_enable;
    ctx->msr_ia32_feature_control = env->msr_ia32_feature_control;
    ctx->spec_ctrl     = env->spec_ctrl;
    ctx->virt_ssbd     = env->virt_ssbd;
    ctx->exception_next_eip = 0;
    ctx->pkru          = env->pkru;
    ctx->tsx_ctrl      = env->tsx_ctrl;
    ctx->df            = env->df;
    ctx->error_code    = 0;
    ctx->exception_is_int = 0;
    wvm_tcg_get_lapic_state(cpu, &ctx->lapic);
    {
        X86XSaveArea xsave;

        x86_cpu_xsave_all_areas(x86_cpu, &xsave);
        ctx->xsave_size = sizeof(xsave);
        memcpy(ctx->xsave_area, &xsave, sizeof(xsave));
    }
}

// Import state from network packet to QEMU TCG
void wvm_tcg_set_state(CPUState *cpu, wvm_tcg_context_t *ctx) {
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    // 1. General Registers
    memcpy(env->regs, ctx->regs, sizeof(env->regs));
    env->eip = ctx->eip;
    cpu_load_eflags(env, (uint32_t)ctx->eflags,
                    CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C |
                    TF_MASK | IF_MASK | DF_MASK | IOPL_MASK |
                    NT_MASK | RF_MASK | VM_MASK | AC_MASK | ID_MASK);
    env->cc_src2 = 0;
    env->cc_dst = 0;

    // 2. Control Registers
    env->cr[0] = ctx->cr[0];
    env->cr[2] = ctx->cr[2];
    env->cr[3] = ctx->cr[3];
    env->cr[4] = ctx->cr[4];
    
    // 3. SSE/AVX Registers
    for (int i = 0; i < 16; i++) {
        env->xmm_regs[i].ZMM_Q(0) = ctx->xmm_regs[i*2];
        env->xmm_regs[i].ZMM_Q(1) = ctx->xmm_regs[i*2 + 1];
    }
    env->mxcsr = ctx->mxcsr;

    /*
     * exit_reason is the previous cpu_exec() return value, not a pending
     * architectural exception.  Re-importing EXCP_HLT/EXCP_INTERRUPT here
     * makes the next cpu_exec() return before it can consume newly pending
     * LAPIC/timer work.
     */
    cpu->exception_index = -1;
    cpu->halted = ctx->halted ? 1 : 0;
    cpu->interrupt_request =
        (cpu->interrupt_request & ~WVM_TCG_INTERRUPT_SYNC_MASK) |
        (ctx->interrupt_request & WVM_TCG_INTERRUPT_SYNC_MASK);
    cpu->interrupt_request &= ~(CPU_INTERRUPT_RESET |
                                CPU_INTERRUPT_INIT |
                                CPU_INTERRUPT_SIPI |
                                CPU_INTERRUPT_POLL);

    // Critical: Flush TB cache to force recompilation with new state
    tb_flush(cpu);

    /* V31: Full segment register state — must be set AFTER tb_flush */
    for (int i = 0; i < 6; i++) {
        env->segs[i].base     = ctx->segs[i].base;
        env->segs[i].limit    = ctx->segs[i].limit;
        env->segs[i].selector = ctx->segs[i].selector;
        env->segs[i].flags    = ctx->segs[i].flags;
    }
    env->ldt.base     = ctx->ldt.base;
    env->ldt.limit    = ctx->ldt.limit;
    env->ldt.selector = ctx->ldt.selector;
    env->ldt.flags    = ctx->ldt.flags;
    env->tr.base      = ctx->tr.base;
    env->tr.limit     = ctx->tr.limit;
    env->tr.selector  = ctx->tr.selector;
    env->tr.flags     = ctx->tr.flags;
    cpu_load_efer(env, ctx->efer);
    env->star         = ctx->star;
    env->sysenter_cs  = ctx->sysenter_cs;
    env->sysenter_esp = ctx->sysenter_esp;
    env->sysenter_eip = ctx->sysenter_eip;
#ifdef TARGET_X86_64
    env->lstar        = ctx->lstar;
    env->cstar        = ctx->cstar;
    env->fmask        = ctx->fmask;
    env->kernelgsbase = ctx->kernelgsbase;
#endif
    env->pat           = ctx->pat;
    env->hflags        = ctx->hflags;
    env->hflags2       = ctx->hflags2;
    env->a20_mask      = ctx->a20_mask;
    env->mp_state      = ctx->mp_state;
    for (int i = 0; i < 7; i++) {
        env->dr[i] = ctx->dr[i];
    }
    cpu_x86_update_dr7(env, ctx->dr[7]);
    env->vm_hsave      = ctx->vm_hsave;
    env->vm_vmcb       = ctx->vm_vmcb;
    env->tsc_offset    = ctx->tsc_offset;
    env->intercept     = ctx->intercept;
    env->nested_cr3    = ctx->nested_cr3;
    env->nested_pg_mode = ctx->nested_pg_mode;
    env->intercept_cr_read = ctx->intercept_cr_read;
    env->intercept_cr_write = ctx->intercept_cr_write;
    env->intercept_dr_read = ctx->intercept_dr_read;
    env->intercept_dr_write = ctx->intercept_dr_write;
    env->intercept_exceptions = ctx->intercept_exceptions;
    env->v_tpr         = ctx->v_tpr;
    memcpy(env->mtrr_fixed, ctx->mtrr_fixed, sizeof(env->mtrr_fixed));
    env->mtrr_deftype  = ctx->mtrr_deftype;
    for (int i = 0; i < WVM_TCG_MTRR_VAR_NB; i++) {
        env->mtrr_var[i].base = ctx->mtrr_var[i].base;
        env->mtrr_var[i].mask = ctx->mtrr_var[i].mask;
    }
    env->system_time_msr = ctx->system_time_msr;
    env->wall_clock_msr = ctx->wall_clock_msr;
    env->steal_time_msr = ctx->steal_time_msr;
    env->async_pf_en_msr = ctx->async_pf_en_msr;
    env->async_pf_int_msr = ctx->async_pf_int_msr;
    env->pv_eoi_en_msr = ctx->pv_eoi_en_msr;
    env->poll_control_msr = ctx->poll_control_msr;
    env->msr_bndcfgs   = ctx->msr_bndcfgs;
    env->exception_nr  = ctx->exception_nr;
    env->interrupt_injected = ctx->interrupt_injected;
    env->soft_interrupt = ctx->soft_interrupt;
    env->nmi_injected  = ctx->nmi_injected;
    env->nmi_pending   = ctx->nmi_pending;
    env->has_error_code = ctx->has_error_code;
    env->exception_pending = ctx->exception_pending;
    env->exception_injected = ctx->exception_injected;
    env->exception_has_payload = ctx->exception_has_payload;
    env->exception_payload = ctx->exception_payload;
    env->ins_len       = ctx->ins_len;
    env->sipi_vector   = ctx->sipi_vector;
    /*
     * Do not import transient exception-delivery state across a remote TCG
     * slice boundary.  cpu->exception_index was already cleared above, so
     * these fields must be neutral as well.
     */
    env->old_exception = -1;
    env->smbase        = ctx->smbase;
    env->tsc           = ctx->tsc;
    env->tsc_adjust    = ctx->tsc_adjust;
    env->tsc_deadline  = ctx->tsc_deadline;
    env->tsc_aux       = ctx->tsc_aux;
    env->xcr0          = ctx->xcr0;
    env->msr_ia32_misc_enable = ctx->msr_ia32_misc_enable;
    env->msr_ia32_feature_control = ctx->msr_ia32_feature_control;
    env->spec_ctrl     = ctx->spec_ctrl;
    env->virt_ssbd     = ctx->virt_ssbd;
    env->exception_next_eip = 0;
    env->pkru          = ctx->pkru;
    env->tsx_ctrl      = ctx->tsx_ctrl;
    env->error_code    = 0;
    env->exception_is_int = 0;
    if (ctx->xsave_size > 0) {
        X86XSaveArea xsave;
        size_t xsave_size = ctx->xsave_size;

        if (xsave_size > sizeof(xsave)) {
            xsave_size = sizeof(xsave);
        }
        memset(&xsave, 0, sizeof(xsave));
        memcpy(&xsave, ctx->xsave_area, xsave_size);
        x86_cpu_xrstor_all_areas(x86_cpu, &xsave);
        update_fp_status(env);
        update_mxcsr_status(env);
        cpu_sync_bndcs_hflags(env);
    }
    wvm_tcg_set_lapic_state(cpu, &ctx->lapic);

    /* Recompute QEMU's hidden execution flags using the native x86 helper.
     * Hand-rolled reconstruction missed CPL/TF/IOPL and can leave imported
     * TCG contexts apparently valid but unable to make guest progress. */
    x86_update_hflags(env);

    /* Keep the legacy base fields in sync for backward compatibility */
    env->segs[R_FS].base = ctx->fs_base;
    env->segs[R_GS].base = ctx->gs_base;
    env->gdt.base = ctx->gdt_base;
    env->gdt.limit = ctx->gdt_limit;
    env->idt.base = ctx->idt_base;
    env->idt.limit = ctx->idt_limit;

    /*
     * CR3/CR0/CR4/EFER and segment hidden state are imported by assignment,
     * bypassing the native helpers that normally invalidate TCG translations.
     * The helper may have reset-vector or previous-slice TLB entries; keeping
     * them across a remote context import makes long-mode page walks use stale
     * translations and can turn a valid remote slice into a false triple fault.
     */
    tlb_flush(cpu);

    {
        static int import_dbg;
        if (import_dbg < 24) {
            fprintf(stderr,
                    "[WVM-TCG-IMPORT] pid=%d cpu=%d eip=%#llx rsp=%#llx "
                    "rflags=%#llx cr0=%#llx cr3=%#llx cr4=%#llx efer=%#llx "
                    "hflags=%#x/%#x oldex=%d env_eip=%#llx env_rsp=%#llx\n",
                    (int)getpid(), cpu->cpu_index,
                    (unsigned long long)ctx->eip,
                    (unsigned long long)ctx->regs[R_ESP],
                    (unsigned long long)ctx->eflags,
                    (unsigned long long)ctx->cr[0],
                    (unsigned long long)ctx->cr[3],
                    (unsigned long long)ctx->cr[4],
                    (unsigned long long)ctx->efer,
                    env->hflags, env->hflags2, env->old_exception,
                    (unsigned long long)env->eip,
                    (unsigned long long)env->regs[R_ESP]);
            import_dbg++;
        }
    }
}

#else

void wvm_tcg_get_state(CPUState *cpu, wvm_tcg_context_t *ctx) {
    (void)cpu;
    memset(ctx, 0, sizeof(*ctx));
}

void wvm_tcg_set_state(CPUState *cpu, wvm_tcg_context_t *ctx) {
    (void)cpu;
    (void)ctx;
}

#endif
