/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/x86-64/asm/include.asm.inc"

START_FILE_INTEL

DECLARE_FUNC(granary_enter_direct_edge)
DECLARE_FUNC(granary_enter_indirect_edge)

// Context switch into granary.
//
// Note: We assume flags are save before this function is invoked.
//
// Note: On entry, `RDI` is a pointer to a `DirectEdge` data structure.
DEFINE_FUNC(granary_arch_enter_direct_edge)
    // Save all regs, except `RDI`.
    push    rax
    push    rsi
    push    rcx
    push    rdx
    push    rbx
    push    rbp
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15

    ALIGN_STACK_16(r15)

    call    granary_enter_direct_edge

    UNALIGN_STACK

    // Restore the regs, except `RDI`.
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rbp
    pop     rbx
    pop     rdx
    pop     rcx
    pop     rsi
    pop     rax
    ret
END_FUNC(granary_arch_enter_direct_edge)

// Context switch into granary. This is used by `edge.asm` to generate a
// profiled and an unprofiled version of the edge entrypoint code. The profiled
// version will increment edge counters, whereas the unprofiled version will
// not.
//
// Note: We assume flags are save before this function is invoked.
//
// Note: On entry, `RDI` is a pointer to a `IndirectEdge` data structure and
//       `RCX` is a pointer to the application code that must be translated.
//
// Note: `RDI` is live on exit, and so must be saved/restored.
DEFINE_FUNC(granary_arch_enter_indirect_edge)
    // Save all regs, except `RCX`.
    push    rax
    push    rsi
    push    rdi
    push    rdx
    push    rbx
    push    rbp
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15

    mov     rsi, rcx  // Move `RCX` into `arg2`.

    ALIGN_STACK_16(r15)

    call    granary_enter_indirect_edge

    UNALIGN_STACK

    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rbp
    pop     rbx
    pop     rdx
    pop     rdi
    pop     rsi
    pop     rax
    ret
END_FUNC(granary_arch_enter_indirect_edge)

END_FILE
