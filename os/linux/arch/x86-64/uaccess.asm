/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/x86-64/asm/include.asm.inc"

START_FILE

    .intel_syntax noprefix

#ifdef GRANARY_WHERE_kernel

// Make reader and writer functions for each memory operand size. These
// functions will read or write to the memory address stored in `RCX`, and if
// the memory operation raises a page fault, then `1` will be stored in `RCX`,
// and if no fault happens then `0` is stored in `RCX`. `RCX` is used so that
// cache code can test if a fault occurred by doing a `JRCXZ`.
//
// The purpose of this is to make the normally exceptional control flow
// explicit.

#define MAKE_READ_FUNC(size, size_name, insn, dest_reg, offs) \
    DEFINE_FUNC(granary_uaccess_read_ ## size)  @N@\
    1:  insn   dest_reg, size_name ptr [rcx]    @N@\
    2:                                          @N@\
    .section .fixup,"ax"                        @N@\
    3:                                          @N@\
        mov     rcx, 1                          @N@\
        ret                                     @N@\
    .section __ex_table,"a"                     @N@\
    .balign 8                                   @N@\
    .long 1b - .,3b - . + offs                  @N@\
    .text                                       @N@\
        mov     rcx, 0                          @N@\
        ret                                     @N@\
    END_FUNC(granary_uaccess_read_ ## size)

#define MAKE_WRITE_FUNC(size, size_name, offs) \
    DEFINE_FUNC(granary_uaccess_write_ ## size) @N@\
    1:  mov   size_name ptr [rcx], 0            @N@\
    2:                                          @N@\
    .section .fixup,"ax"                        @N@\
    3:                                          @N@\
        mov     rcx, 1                          @N@\
        ret                                     @N@\
    .section __ex_table,"a"                     @N@\
    .balign 8                                   @N@\
    .long 1b - .,3b - . + offs                  @N@\
    .text                                       @N@\
        mov     rcx, 0                          @N@\
        ret                                     @N@\
    END_FUNC(granary_uaccess_write_ ## size)

MAKE_READ_FUNC(8, byte, movzx, ecx, 0)
MAKE_READ_FUNC(16, word, movzx, ecx, 0)
MAKE_READ_FUNC(32, dword, mov, ecx, 0)
MAKE_READ_FUNC(64, qword, mov, rcx, 0)

MAKE_WRITE_FUNC(8, byte, 0)
MAKE_WRITE_FUNC(16, word, 0)
MAKE_WRITE_FUNC(32, dword, 0)
MAKE_WRITE_FUNC(64, qword, 0)

MAKE_READ_FUNC(error_8, byte, movzx, ecx, 0x7ffffff0)
MAKE_READ_FUNC(error_16, word, movzx, ecx, 0x7ffffff0)
MAKE_READ_FUNC(error_32, dword, mov, ecx, 0x7ffffff0)
MAKE_READ_FUNC(error_64, qword, mov, rcx, 0x7ffffff0)

MAKE_WRITE_FUNC(error_8, byte, 0x7ffffff0)
MAKE_WRITE_FUNC(error_16, word, 0x7ffffff0)
MAKE_WRITE_FUNC(error_32, dword, 0x7ffffff0)
MAKE_WRITE_FUNC(error_64, qword, 0x7ffffff0)

#endif  // GRANARY_WHERE_kernel

END_FILE
