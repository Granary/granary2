/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/arch/x86-64/asm/include.asm.inc"

START_FILE

DECLARE_FUNC(granary_enter_direct_edge)
DECLARE_FUNC(granary_enter_indirect_edge)

#define DirectEdge_num_executions 16
#define DirectEdge_num_execution_overflows 20

// Context switch into granary. This allows the runtime to select a profiled
// and an unprofiled version of the edge entrypoint code. The profiled
// version will increment edge counters, whereas the unprofiled version will
// not.
//
// Note: We assume flags are save before this function is invoked.
//
// Note: On entry, `RDI` is a pointer to a `DirectEdge` data structure, and
//       `RSI` is a pointer to the `ContextInferface` data structure.
DEFINE_FUNC(granary_arch_enter_direct_edge)

    // If we've already translated the target block, then increment the
    // execution counter. This is a saturating counter, where one counter
    // counts up to 32 bits, and the other counts the number of overflows.
    push    %rax
    mov     $1, %rax
    xadd    %rax, DirectEdge_num_executions(%rdi)
    jo      .Ledge_counter_overflowed

    test    %rax, %rax
    jnz     .Lback_to_code_cache  // Already executed

    // We'll assume that if the value before incrementing was zero that we "won"
    // and that if two threads get in, then it is either because there is enough
    // contention on this code to cause an overflow, or because we're in user
    // space and we were de-scheduled, thus making time for the overflow to
    // occur.
    jmp     .Ltranslate_block

  .Ledge_counter_overflowed:
    // An unlikely event, so no need to lock the cache line.
    incl    DirectEdge_num_execution_overflows(%rdi)

    // If we overflowed then we know that the edge has already been resolved,
    // as it has already been executed at least 2^32 times.
    jmp     .Lback_to_code_cache

  .Ltranslate_block:
    // Save all regs, except `RDI` and `RSI`.
    push    %rcx
    push    %rdx
    push    %rbx
    push    %rbp
    push    %r8
    push    %r9
    push    %r10
    push    %r11
    push    %r12
    push    %r13
    push    %r14
    push    %r15

    // Align the stack to a 16-byte boundary.
    push    %rsp
    push    (%rsp)
    and     $-16, %rsp

    GRANARY_IF_USER(PUSHA_XMM)

    call    granary_enter_direct_edge

    GRANARY_IF_USER(POPA_XMM)

    // Restore the old stack alignment.
    mov     8(%rsp), %rsp

    // Restore the regs, except `RDI` and `RSI`.
    pop     %r15
    pop     %r14
    pop     %r13
    pop     %r12
    pop     %r11
    pop     %r10
    pop     %r9
    pop     %r8
    pop     %rbp
    pop     %rbx
    pop     %rdx
    pop     %rcx

  .Lback_to_code_cache:
    pop     %rax
    ret
END_FUNC(granary_arch_enter_direct_edge)

// Context switch into granary. This is used by `edge.asm` to generate a
// profiled and an unprofiled version of the edge entrypoint code. The profiled
// version will increment edge counters, whereas the unprofiled version will
// not.
//
// Note: We assume flags are save before this function is invoked.
//
// Note: On entry, `RDI` is a pointer to a `DirectEdge` data structure,
//       `RSI` is a pointer to the `ContextInferface` data structure, and
//       `RCX` is a pointer to the application code that must be translated.
//
// Note: `RDI` is live on exit, and so must be saved/restored.
DEFINE_FUNC(granary_arch_enter_indirect_edge)
    // Save all regs, except `RSI`, and `RCX`.
    push    %rax
    push    %rdi
    push    %rdx
    push    %rbx
    push    %rbp
    push    %r8
    push    %r9
    push    %r10
    push    %r11
    push    %r12
    push    %r13
    push    %r14
    push    %r15

    mov     %rcx, %rdx  // Move `RCX` into `arg3`.

    // Align the stack to a 16-byte boundary.
    push    %rsp
    push    (%rsp)
    and     $-16, %rsp

    GRANARY_IF_USER(PUSHA_XMM)

    call    granary_enter_indirect_edge

    GRANARY_IF_USER(POPA_XMM)

    // Restore the old stack alignment.
    mov     8(%rsp), %rsp

    pop     %r15
    pop     %r14
    pop     %r13
    pop     %r12
    pop     %r11
    pop     %r10
    pop     %r9
    pop     %r8
    pop     %rbp
    pop     %rbx
    pop     %rdx
    pop     %rdi
    pop     %rax
    ret
END_FUNC(granary_arch_enter_indirect_edge)

END_FILE
