
#include "granary/base/base.h"

#define START_FILE \
    .text
#define START_FILE_INTEL \
    .intel_syntax noprefix @N@\
    .text
    
#define END_FILE
#define SYMBOL(x) x

#define GLOBAL_LABEL(x) SYMBOL(x)

#define DECLARE_FUNC(x) \
    .extern SYMBOL(x)

#define DEFINE_FUNC(x) \
    .text @N@\
    .align 16 @N@\
    .globl SYMBOL(x) @N@\
    .type SYMBOL(x), @function @N@\
    x: @N@\
    .cfi_startproc

#define DEFINE_INST_FUNC(x) \
    .section .text.inst_exports @N@\
    .align 16 @N@\
    .globl SYMBOL(x) @N@\
    .type SYMBOL(x), @function @N@\
    x: @N@\
    .cfi_startproc

#define END_FUNC(x) \
    ud2 @N@\
    .cfi_endproc @N@
    
#define HEX(v) $0x ## v
#define PUSHF pushf
#define POPF popf
#define REG_XAX rax
#define REG_XBX rbx
#define REG_XCX rcx
#define REG_XDX rdx
#define REG_XSI rsi
#define REG_XDI rdi
#define REG_XBP rbp
#define REG_XSP rsp
#define ARG1 rdi
#define ARG2 rsi
#define ARG3 rdx
#define ARG4 rcx
#define ARG5 r8
#define ALL_XMM_REGS(R, R_last) \
    R(240, xmm0, \
    R(224, xmm1, \
    R(208, xmm2, \
    R(192, xmm3, \
    R(176, xmm4, \
    R(160, xmm5, \
    R(144, xmm6, \
    R(128, xmm7, \
    R(112, xmm8, \
    R(96,  xmm9, \
    R(80,  xmm10, \
    R(64,  xmm11, \
    R(48,  xmm12, \
    R(32,  xmm13, \
    R(16,  xmm14, \
    R_last(0, xmm15))))))))))))))))

#define PUSH_LAST_XMM_REG(off, reg) \
    movaps %reg, off(%rsp);

#define PUSH_XMM_REG(off, reg, rest) \
    PUSH_LAST_XMM_REG(off, reg) \
    rest

#define POP_LAST_XMM_REG(off, reg) \
    movaps off(%rsp), %reg;

#define POP_XMM_REG(off, reg, rest) \
    rest \
    POP_LAST_XMM_REG(off, reg)

// Used for counting the space needed to store all registers.
#define PLUS_8_8 16
#define PLUS_8_16 24
#define PLUS_8_24 32
#define PLUS_8_32 40
#define PLUS_8_40 48
#define PLUS_8_48 56
#define PLUS_8_56 64
#define PLUS_8_64 72
#define PLUS_8_72 80
#define PLUS_8_80 88
#define PLUS_8_88 96
#define PLUS_8_96 104
#define PLUS_8_104 112
#define PLUS_8_112 120
#define PLUS_8_120 128
#define PLUS_8_128 136
#define PLUS_8_136 144
#define PLUS_8_144 152
#define PLUS_8_152 160

#define PLUS_EIGHT(_,rest) CAT(PLUS_8_, rest)
#define EIGHT(_) 8

// Used to save and restore registers
#define PUSHA ALL_REGS(PUSH_REG, PUSH_LAST_REG)
#define POPA ALL_REGS(POP_REG, POP_LAST_REG)

// Used to save and restore XMM registers. Note: we assume that %rsp is
// 16-byte aligned.
#define PUSHA_XMM \
    lea -256(%rsp), %rsp; \
    ALL_XMM_REGS(PUSH_XMM_REG, PUSH_LAST_XMM_REG)

#define POPA_XMM \
    ALL_XMM_REGS(POP_XMM_REG, POP_LAST_XMM_REG) \
    lea 256(%rsp), %rsp; \
