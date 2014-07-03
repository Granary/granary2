/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "test/arch/x86-64/util/include.S"

    .text

#define CF (1 << 0)  // carry
#define PF (1 << 2)  // parity
#define AF (1 << 4)  // adjust
#define ZF (1 << 6)  // zero
#define SF (1 << 7)  // sign
#define DF (1 << 10)  // direction
#define OF (1 << 11)  // overflow

// For each jump type, expand some macro with enough info to generate test
// code.
//
// Note: Not all possible condition codes need to be set here (especially for
//       things like jle where one of two conditions can be met); only one
//       satisfying condition needs to be met.
#define FOR_EACH_CBR(macro, ...) \
    macro(jo, OF, ~0, ##__VA_ARGS__) \
    macro(jno, 0, ~OF, ##__VA_ARGS__) \
    macro(jb, CF, ~0, ##__VA_ARGS__) \
    macro(jnb, 0, ~CF, ##__VA_ARGS__) \
    macro(jz, ZF, ~0, ##__VA_ARGS__) \
    macro(jnz, 0, ~ZF, ##__VA_ARGS__) \
    macro(jbe, (CF | ZF), ~0, ##__VA_ARGS__) \
    macro(jnbe, 0, ~(CF | ZF), ##__VA_ARGS__) \
    macro(js, SF, ~0, ##__VA_ARGS__) \
    macro(jns, 0, ~SF, ##__VA_ARGS__) \
    macro(jp, PF, ~0, ##__VA_ARGS__) \
    macro(jnp, 0, ~PF, ##__VA_ARGS__) \
    macro(jl, SF, ~0, ##__VA_ARGS__) \
    macro(jnl, 0, ~SF, ##__VA_ARGS__) \
    macro(jle, (ZF | SF), ~0, ##__VA_ARGS__) \
    macro(jnle, 0, ~(ZF | SF), ##__VA_ARGS__)


// Make a test for a conditional branch opcode.
#define MAKE_CBR_TEST_FUNC(opcode, or_flag, and_flag) \
    BEGIN_FUNC(jcc_ ## opcode ## _true) \
        xor     %eax, %eax; \
        pushf; \
        movq    $or_flag, %rax; \
        or      %rax, (%rsp); \
        movq    $and_flag, %rax; \
        and     %rax, (%rsp); \
        popf; \
        opcode  1f; \
        mov     $0, %rax; \
        jmp     2f; \
    1:  mov     $1, %rax; \
    2:  ret; \
    END_FUNC \
   ; \
    BEGIN_FUNC(jcc_ ## opcode ## _false) \
        xor     %eax, %eax; \
        pushf; \
        movq    $~and_flag, %rax; \
        or      %rax, (%rsp); \
        movq    $~or_flag, %rax; \
        and     %rax, (%rsp); \
        popf; \
        opcode  1f; \
        mov     $1, %rax; \
        jmp     2f; \
    1:  mov     $0, %rax; \
    2:  ret; \
    END_FUNC

FOR_EACH_CBR(MAKE_CBR_TEST_FUNC)

BEGIN_FUNC(jcc_jecxz_true)
    xor     %eax, %eax;
    mov     $0, %rcx;
    jecxz   1f;
    mov     $0, %rax;
    jmp     2f;
1:  mov     $1, %rax;
2:  ret;
END_FUNC

BEGIN_FUNC(jcc_jecxz_false)
    mov     $1, %rcx;
    jecxz   1f;
    mov     $1, %rax;
    jmp     2f;
1:  mov     $0, %rax;
2:  ret;
END_FUNC

BEGIN_FUNC(jcc_jrcxz_true)
    xor     %eax, %eax;
    mov     $0, %rcx;
    jrcxz   1f;
    mov     $0, %rax;
    jmp     2f;
1:  mov     $1, %rax;
2:  ret;
END_FUNC

BEGIN_FUNC(jcc_jrcxz_false)
    mov     $1, %rcx;
    jrcxz   1f;
    mov     $1, %rax;
    jmp     2f;
1:  mov     $0, %rax;
2:  ret;
END_FUNC

BEGIN_FUNC(loop_return_5)
    mov     $0, %rax;
    mov     $5, %rcx;
1:  inc     %rax;
    loop    2f;
    jmp     3f;
2:  jmp     1b;
3:  ret;
END_FUNC

BEGIN_FUNC(loope_return_5)
    mov     $0, %rax;
    mov     $5, %rcx;
1:  inc     %rax;
    xor     %edx, %edx;  // Ensure ZF=1.
    loope   2f;
    jmp     3f;
2:  jmp     1b;
3:  ret;
END_FUNC

BEGIN_FUNC(loopne_return_5)
    mov     $0, %rax;
    mov     $5, %rcx;
1:  inc     %rax;
    loopne  2f;  // Will test the flags set by the `INC`, which will have ZF=0.
    jmp     3f;
2:  jmp     1b;
3:  ret;
END_FUNC
