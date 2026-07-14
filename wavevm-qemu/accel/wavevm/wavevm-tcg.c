
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
    (CPU_INTERRUPT_HARD | CPU_INTERRUPT_HALT | CPU_INTERRUPT_RESET | \
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
    ctx->eflags = env->eflags;

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
}

// Import state from network packet to QEMU TCG
void wvm_tcg_set_state(CPUState *cpu, wvm_tcg_context_t *ctx) {
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    // 1. General Registers
    memcpy(env->regs, ctx->regs, sizeof(env->regs));
    env->eip = ctx->eip;
    env->eflags = ctx->eflags;

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

    cpu->exception_index = ctx->exit_reason;
    cpu->halted = ctx->halted ? 1 : 0;
    cpu->interrupt_request =
        (cpu->interrupt_request & ~WVM_TCG_INTERRUPT_SYNC_MASK) |
        (ctx->interrupt_request & WVM_TCG_INTERRUPT_SYNC_MASK);

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

    /* Recompute hflags from the restored segment/CR state so TCG
     * generates correct code for the current CPU mode (real/prot/long). */
    env->hflags = 0;
    if (env->cr[0] & 1) {  /* CR0.PE */
        env->hflags |= HF_PE_MASK;
        if (env->eflags & VM_MASK) {
            env->hflags |= HF_VM_MASK;
        }
        if (env->cr[4] & CR4_PAE_MASK) {
            env->hflags |= HF_LMA_MASK * !!(env->efer & MSR_EFER_LMA);
        }
        if (env->efer & MSR_EFER_LMA) {
            if (env->segs[R_CS].flags & DESC_L_MASK) {
                env->hflags |= HF_CS64_MASK | HF_CS32_MASK;
            } else if (env->segs[R_CS].flags & DESC_B_MASK) {
                env->hflags |= HF_CS32_MASK;
            }
            env->hflags |= HF_LMA_MASK;
        } else {
            if (env->segs[R_CS].flags & DESC_B_MASK) {
                env->hflags |= HF_CS32_MASK;
            }
            if (env->segs[R_SS].flags & DESC_B_MASK) {
                env->hflags |= HF_SS32_MASK;
            }
        }
    }
    if (env->cr[0] & CR0_TS_MASK) {
        env->hflags |= HF_TS_MASK;
    }
    if (env->cr[0] & CR0_EM_MASK) {
        env->hflags |= HF_EM_MASK;
    }
    if (env->cr[0] & CR0_MP_MASK) {
        env->hflags |= HF_MP_MASK;
    }
    if (env->cr[4] & CR4_OSFXSR_MASK) {
        env->hflags |= HF_OSFXSR_MASK;
    }

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

