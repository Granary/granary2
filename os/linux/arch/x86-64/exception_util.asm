/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/x86-64/asm/include.asm.inc"

START_FILE_INTEL

#ifdef GRANARY_WHERE_kernel

// A REP MOVS that can fault.
#define MAKE_REP_MOVS(size_bytes, size_name, offs) \
    DEFINE_INST_FUNC(granary_extable_rep_movs_ ## size_bytes)               @N@\
        1:  rep movs size_name PTR es:[rdi],size_name PTR ds:[rsi]          @N@\
        2:                                                                  @N@\
        .section .fixup,"ax"                                                @N@\
        3:  pushfq                                                          @N@\
            add QWORD PTR [rsp + 8], 5                                      @N@\
            popfq                                                           @N@\
            ret                                                             @N@\
        .section __ex_table,"a"                                             @N@\
        .balign 8                                                           @N@\
        .long 1b - .,3b - . + offs                                          @N@\
        .section .text.inst_exports                                         @N@\
            ret                                                             @N@\
    END_FUNC(granary_extable_rep_movs_ ## size_bytes)

MAKE_REP_MOVS(8, BYTE, 0)
MAKE_REP_MOVS(16, WORD, 0)
MAKE_REP_MOVS(32, DWORD, 0)
MAKE_REP_MOVS(64, QWORD, 0)

// A write to a segment register that can fault.
#define MAKE_SEG_WRITE_FUNC(seg) \
    DEFINE_INST_FUNC(granary_extable_write_seg_ ## seg)                     @N@\
            push  rsi                                                       @N@\
            mov   rsi, QWORD PTR [rsp + 16]                                 @N@\
        1:  mov   seg, esi                                                  @N@\
        2:                                                                  @N@\
        .section .fixup,"ax"                                                @N@\
        3:  pushfq                                                          @N@\
            add   QWORD PTR [rsp + 16], 5                                   @N@\
            popfq                                                           @N@\
            pop   rsi                                                       @N@\
            ret                                                             @N@\
            .section __ex_table,"a"                                         @N@\
        .section __ex_table,"a"                                             @N@\
        .balign 8                                                           @N@\
        .long 1b - .,3b - .                                                 @N@\
        .section .text.inst_exports                                         @N@\
            pop   rsi                                                       @N@\
            ret                                                             @N@\
    END_FUNC(granary_extable_write_seg_ ## seg)

MAKE_SEG_WRITE_FUNC(fs)
MAKE_SEG_WRITE_FUNC(gs)
MAKE_SEG_WRITE_FUNC(cs)
MAKE_SEG_WRITE_FUNC(ds)
MAKE_SEG_WRITE_FUNC(es)
MAKE_SEG_WRITE_FUNC(ss)

#define rsi_byte    sil
#define rsi_word    si
#define rsi_dword   esi
#define rsi_qword   rsi

#define MAKE_WRITE_FUNC(op, size, size_name, offs) \
    DEFINE_INST_FUNC(granary_extable_write_ ## size)                        @N@\
          push  rdi                                                         @N@\
          push  rsi                                                         @N@\
          mov   rdi, qword ptr [rsp + 32]                                   @N@\
          mov   rsi, qword ptr [rsp + 24]                                   @N@\
      1:  op   size_name ptr [rdi], rsi_ ## size_name                       @N@\
      2:                                                                    @N@\
      .section .fixup,"ax"                                                  @N@\
      3:  pushfq                                                            @N@\
          add   QWORD PTR [rsp + 24], 5                                     @N@\
          popfq                                                             @N@\
          pop   rsi                                                         @N@\
          pop   rdi                                                         @N@\
          ret                                                               @N@\
      .section __ex_table,"a"                                               @N@\
      .balign 8                                                             @N@\
      .long 1b - .,3b - . + offs                                            @N@\
      .section .text.inst_exports                                           @N@\
          pop   rsi                                                         @N@\
          pop   rdi                                                         @N@\
          ret                                                               @N@\
    END_FUNC(granary_extable_write_ ## size)

MAKE_WRITE_FUNC(mov, 8, byte, 0)
MAKE_WRITE_FUNC(mov, 16, word, 0)
MAKE_WRITE_FUNC(mov, 32, dword, 0)
MAKE_WRITE_FUNC(mov, 64, qword, 0)

MAKE_WRITE_FUNC(mov, error_8, byte, 0x7ffffff0)
MAKE_WRITE_FUNC(mov, error_16, word, 0x7ffffff0)
MAKE_WRITE_FUNC(mov, error_32, dword, 0x7ffffff0)
MAKE_WRITE_FUNC(mov, error_64, qword, 0x7ffffff0)

MAKE_WRITE_FUNC(xchg, 8, byte, 0)
MAKE_WRITE_FUNC(xchg, 16, word, 0)
MAKE_WRITE_FUNC(xchg, 32, dword, 0)
MAKE_WRITE_FUNC(xchg, 64, qword, 0)

#define MAKE_INSTR_FUNC(insn) \
    DEFINE_INST_FUNC(granary_extable_ ## insn)                              @N@\
      1:  insn                                                              @N@\
      2:                                                                    @N@\
      .section .fixup,"ax"                                                  @N@\
      3:                                                                    @N@\
          pushfq                                                            @N@\
          add   QWORD PTR [rsp + 8], 5                                      @N@\
          popfq                                                             @N@\
          ret                                                               @N@\
      .section __ex_table,"a"                                 @N@\
      .balign 8                               @N@\
      .long 1b - .,3b - .                                                   @N@\
      .section .text.inst_exports                                           @N@\
          ret                                 @N@\
    END_FUNC(granary_extable_ ## insn)

MAKE_INSTR_FUNC(rdmsr)
MAKE_INSTR_FUNC(wrmsr)
MAKE_INSTR_FUNC(fwait)


#define MAKE_READ_FUNC(size, size_name, offs) \
    DEFINE_INST_FUNC(granary_extable_read_ ## size)                         @N@\
          push  rdi                                                         @N@\
          push  rsi                                                         @N@\
          mov   rdi, qword ptr [rsp + 32]                                   @N@\
          mov   rsi, qword ptr [rsp + 24]                                   @N@\
      1:  mov   rsi_ ## size_name, size_name ptr [rdi]                      @N@\
      2:                                                                    @N@\
      .section .fixup,"ax"                                                  @N@\
      3:  pushfq                                                            @N@\
          add   QWORD PTR [rsp + 24], 5                                     @N@\
          popfq                                                             @N@\
          pop   rsi                                                         @N@\
          pop   rdi                                                         @N@\
          ret                                                               @N@\
      .section __ex_table,"a"                                               @N@\
      .balign 8                                                             @N@\
      .long 1b - .,3b - . + offs                                            @N@\
      .section .text.inst_exports                                           @N@\
          pop   rsi                                                         @N@\
          pop   rdi                                                         @N@\
          ret                                                               @N@\
    END_FUNC(granary_extable_read_ ## size)

#if 0
// Make reader and writer functions for each memory operand size. These
// functions will read or write to the memory address stored in `RCX`, and if
// the memory operation raises a page fault, then `1` will be stored in `RCX`,
// and if no fault happens then `0` is stored in `RCX`. `RCX` is used so that
// cache code can test if a fault occurred by doing a `JRCXZ`.
//
// The purpose of this is to make the normally exceptional control flow
// explicit.

#define MAKE_READ_FUNC(size, size_name, insn, dest_reg, offs) \
    DEFINE_FUNC(granary_extable_read_ ## size)  @N@\
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
    END_FUNC(granary_extable_read_ ## size)






MAKE_READ_FUNC(8, byte, movzx, ecx, 0)
MAKE_READ_FUNC(16, word, movzx, ecx, 0)
MAKE_READ_FUNC(32, dword, mov, ecx, 0)
MAKE_READ_FUNC(64, qword, mov, rcx, 0)



MAKE_READ_FUNC(error_8, byte, movzx, ecx, 0x7ffffff0)
MAKE_READ_FUNC(error_16, word, movzx, ecx, 0x7ffffff0)
MAKE_READ_FUNC(error_32, dword, mov, ecx, 0x7ffffff0)
MAKE_READ_FUNC(error_64, qword, mov, rcx, 0x7ffffff0)






#endif

#endif  // GRANARY_WHERE_kernel

END_FILE
