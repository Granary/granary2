/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/arch/x86-64/asm/include.asm.inc"

START_FILE


DEFINE_FUNC(granary_memcpy)
    movq %rdx, %rcx;
    movq %rdi, %rdx;
    rep movsb;
    movq %rdx, %rax;
    retq;
END_FUNC(granary_memcpy)


DEFINE_FUNC(granary_memset)
    movq %rsi, %rax;
    movq %rdx, %rcx;
    movq %rdi, %rdx;
    rep stosb;
    movq %rdx, %rax;
    retq;
END_FUNC(granary_memset)


DEFINE_FUNC(granary_memcmp)
.Lgranary_memcmp_next_byte:
    test %rdx, %rdx;
    movq $0, %rax;
    jz .Lgranary_memcmp_done;

    movb (%rdi), %al;
    subb (%rsi), %al;
    jnz .Lgranary_memcmp_done;

    sub $1, %rdx;
    add $1, %rdi;
    add $1, %rsi;
    jmp .Lgranary_memcmp_next_byte;

.Lgranary_memcmp_done:
    retq;
END_FUNC(granary_memcmp)


END_FILE
