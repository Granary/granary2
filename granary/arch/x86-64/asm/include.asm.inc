
#include "granary/base/base.h"

#define START_FILE .text
#define END_FILE
#define SYMBOL(x) x

#define GLOBAL_LABEL(x) SYMBOL(x)

#define DECLARE_FUNC(x) \
    .extern SYMBOL(x)

#define DEFINE_FUNC(x) \
    .align 16 @N@\
    .globl SYMBOL(x) @N@\
    .type SYMBOL(x), @function @N@\
    x: @N@\
    .cfi_startproc

#define END_FUNC(x) \
    ud2 @N@\
    .cfi_endproc @N@\

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

