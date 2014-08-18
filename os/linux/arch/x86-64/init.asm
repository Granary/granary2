/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/x86-64/asm/include.asm.inc"

START_FILE

    .intel_syntax noprefix

#ifdef GRANARY_WHERE_user

DECLARE_FUNC(granary_init)
DECLARE_FUNC(granary_exit_group)

.section .init
.global _init
.type _init, @function
_init:
    .cfi_startproc
    mov     rdi, rsp
    jmp     granary_init
    ud2
    .cfi_endproc

.section .fini
.global _fini
.type _fini, @function
_fini:
    .cfi_startproc
    jmp granary_exit_group
    ud2
    .cfi_endproc

#endif  // GRANARY_WHERE_user

END_FILE
