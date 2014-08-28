/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/x86-64/asm/include.asm.inc"

START_FILE_INTEL

DECLARE_FUNC(granary_trace_block_regs)

DEFINE_FUNC(granary_trace_block)
    // Saved IP is already on the stack in the form of the return address.

    // Slightly offsetted stack pointer, we will correct this in
    // `granary_trace_block_regs`.
    push rsp

    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    pushfq
    push 0  // Temporary thread ID.
    lea rdi, [rsp]
    GRANARY_IF_KERNEL( cli )  // Disable interrupts.
    call granary_trace_block_regs
    lea rsp, [rsp + 8]  // Space for thread ID.
    popfq
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    lea rsp, [rsp + 8]
    ret
END_FUNC(granary_trace_block)

END_FILE
