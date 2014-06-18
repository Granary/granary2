/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/arch/x86-64/asm/include.asm.inc"

START_FILE

DECLARE_FUNC(granary_enter_direct_edge)

#define DirectEdge_num_executions 8
#define DirectEdge_num_execution_overflows 12

// Context switch into granary. This is used by `edge.asm` to generate a
// profiled and an unprofiled version of the edge entrypoint code. The profiled
// version will increment edge counters, whereas the unprofiled version will
// not.
//
// Note: We assume flags are save before this function is invoked.
//
// Note: On entry, `RDI` is a pointer to a `DirectEdge` data structure.
DEFINE_FUNC(granary_arch_enter_direct_edge)
    // Save the flags.
    pushfq

    // Disable interrupts. This still leaves two opportunities for interruption.
    //      1) In the edge code, after the stack switch, and in this function
    //         before the `CLI`.
    //      2) In this code after the `POPF`, and in the edge code before the
    //         stack switch back to native.
    GRANARY_IF_KERNEL( cli )

    // If we've already translated the target block, then increment the
    // execution counter. This is a saturating counter, where one counter
    // counts up to 32 bits, and the other counts the number of overflows.
    push    %rsi
    mov     $1, %rsi
    xadd    %rsi, DirectEdge_num_executions(%rdi)
    jo      .Ledge_counter_overflowed

    test    %rsi, %rsi
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
    // Save all regs, except `RDI`.
    push    %rax
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

    call    granary_enter_direct_edge

    // Restore the old stack alignment.
    pop     %rsp

    // Restore the regs (except `RDI`)
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
    pop     %rax

  .Lback_to_code_cache:
    pop     %rsi
    popfq  // Will restore interrupts if disabled.
    ret
END_FUNC(granary_arch_enter_direct_edge)

END_FILE
