/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "test/arch/x86-64/util/include.S"

    .text

BEGIN_FUNC(RunFunctionInContext)

    /* Copy over new regs state */
    xchg 0(%rsi), %rsp;     /* swap stacks. */
    push %rsi;              /* reg state pointer for state save & restore */
    push %rdi;              /* function to call */

    xchg 8(%rsi), %rax;
    xchg 16(%rsi), %rcx;
    xchg 24(%rsi), %rdx;
    xchg 32(%rsi), %rbx;
    xchg 40(%rsi), %rbp;
                            /* rdi would normally be here... */
    xchg 56(%rsi), %rdi;
    xchg 64(%rsi), %r8;
    xchg 72(%rsi), %r9;
    xchg 80(%rsi), %r10;
    xchg 88(%rsi), %r11;
    xchg 96(%rsi), %r12;
    xchg 104(%rsi), %r13;
    xchg 112(%rsi), %r14;
    xchg 120(%rsi), %r15;
    xchg 48(%rsi), %rsi;    /* ...but it's down here. */

    /* Zero out the aflags */
    push %rax;
    xor %rax, %rax;
    sahf;
    pop %rax;
    cld;

    /* Overwrite the target of the call on the stack, assumes redzone */
    lea 8(%rsp), %rsp;
    call *-8(%rsp);

    /* Save aflags, so that they get compared as well */
    pushq $0;
    /*
    // Note: Disabled because it introduces flakiness with things like
    //       `ADD N, RSP` being converted into `TEST RSP, RSP` (similar
    //       flakiness with converting to `ADD 0, RSP`. Things that end up
    //       differing in practice are the parity flag and the BCD adjust / aux.
    //       carry flag.
    pushq %rax;
    mov $0, %rax;
    lahf;
    mov %rax, 8(%rsp);
    pop %rax;
    */

    xchg 8(%rsp), %rsp;      /* restore reg state pointer */

    /* Save current state and restore native state */
    xchg 8(%rsp), %rax;
    xchg 16(%rsp), %rcx;
    xchg 24(%rsp), %rdx;
    xchg 32(%rsp), %rbx;
    xchg 40(%rsp), %rbp;
    xchg 48(%rsp), %rsi;
    xchg 56(%rsp), %rdi;
    xchg 64(%rsp), %r8;
    xchg 72(%rsp), %r9;
    xchg 80(%rsp), %r10;
    xchg 88(%rsp), %r11;
    xchg 96(%rsp), %r12;
    xchg 104(%rsp), %r13;
    xchg 112(%rsp), %r14;
    xchg 120(%rsp), %r15;

    mov (%rsp), %rsp;      /* swap back to native stack */

    ret;
END_FUNC
