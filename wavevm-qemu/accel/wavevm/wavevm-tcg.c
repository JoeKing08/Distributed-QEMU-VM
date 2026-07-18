
#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "../../../common_include/wavevm_protocol.h"

#if defined(TARGET_I386) || defined(TARGET_X86_64)

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

// Export QEMU TCG state to network packet
void wvm_tcg_get_state(CPUState *cpu, wvm_tcg_context_t *ctx) {
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

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
    ctx->hflags2       = env->hflags2;
    ctx->a20_mask      = env->a20_mask;
    ctx->mp_state      = env->mp_state;
    ctx->old_exception = env->old_exception;
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
    ctx->exception_next_eip = env->exception_next_eip;
    ctx->pkru          = env->pkru;
    ctx->tsx_ctrl      = env->tsx_ctrl;
    ctx->df            = env->df;
    ctx->error_code    = env->error_code;
    ctx->exception_is_int = env->exception_is_int;
}

// Import state from network packet to QEMU TCG
void wvm_tcg_set_state(CPUState *cpu, wvm_tcg_context_t *ctx) {
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    // 1. General Registers
    memcpy(env->regs, ctx->regs, sizeof(env->regs));
    env->eip = ctx->eip;
    env->eflags = ctx->eflags;
    env->cc_op = CC_OP_EFLAGS;
    env->cc_src = 0;
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
    env->efer         = ctx->efer;
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
    env->hflags2       = ctx->hflags2;
    env->a20_mask      = ctx->a20_mask;
    env->mp_state      = ctx->mp_state;
    env->old_exception = ctx->old_exception;
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
    env->exception_next_eip = ctx->exception_next_eip;
    env->pkru          = ctx->pkru;
    env->tsx_ctrl      = ctx->tsx_ctrl;
    env->df            = ctx->df;
    env->error_code    = ctx->error_code;
    env->exception_is_int = ctx->exception_is_int;

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
