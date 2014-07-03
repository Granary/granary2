/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/arch/x86-64/asm/include.asm.inc"

START_FILE

.Lgranary_arch_Init:
  ret;
.LLoadClients:
  ret;
.LContainer_Environment_Construct:
  ret;
.LUnsafeCast:
  ret;
.LContainer_Access:
  ret;
.LEnvironment_Setup:
  ret;
.LEnvironment_AttachToAppPC:
  ret;

// Defines a function that is used to test some of the early instruction
// mangling of stack-pointer changing instructions.
DEFINE_FUNC(granary_test_mangle)
mov    -0x18(%rbp),%rsi
mov    -0x18(%rbp),%rsi
call *%rax;
ret;
/*
push   %rbp
mov    %rsp,%rbp
sub    $0x20,%rsp
mov    %rdi,-0x8(%rbp)
callq  .Lgranary_arch_Init
mov    -0x8(%rbp),%rdi
callq  .LLoadClients
1:
lea    0x3564cf(%rip),%rdi
movslq 0x356498(%rip),%rax
imul   $0x640,%rax,%rax
add    %rax,%rdi
mov    %rdi,-0x10(%rbp)
mov    -0x10(%rbp),%rdi
callq  .LContainer_Environment_Construct
lea    0x387da(%rip),%rdi
callq  .LUnsafeCast
jz 1b
mov    %rax,-0x18(%rbp)
mov    -0x10(%rbp),%rdi
callq  .LContainer_Access
2:
mov     %rax,%rdi
call    *%rdi;
jz 2b
callq  .LEnvironment_Setup
mov    -0x10(%rbp),%rdi
callq  .LContainer_Access
mov    -0x18(%rbp),%rsi
mov    %rax,%rdi
callq  .LEnvironment_AttachToAppPC
add    $0x20,%rsp
pop    %rbp
retq
*/
#if 0
    //movslq 0x7db9cb, %rax;
    mov 0xABCAFE0000, %rax;
    mov 0xCAFE0000, %eax;
    /*

    mov $0xCAFE0000, %rax;
    mov $0xCAFE0000, %eax; */
    //jmp *%gs:(%rax);
    pushq $0;
    pushq $0xBEEF;
    pop (%rsp);
    //call *%rbx;
    call 1f
    jmp 2f
   1:
    ret
   2:
    //call .Lgranary_arch_Init;
    lea 8(%rsp), %rsp;
    //movq   -0x140(%rbp),%xmm0

    ret;
/*
    mov -168(%rax), %rax;
    mov 8(%rax), %rax;
    mov -8(%rax), %rax;

    movsb;
    xlat;

    lea .Lgranary_arch_Init(%rip), %rax;
    movslq 0x7db9cb, %rax;
    mov 0xCAFE0000, %rax;
    movzbl %al, %ebx;
    mov %ecx, %gs:0xABADC0;
    mov %gs:(%rcx), %rdx;
    mov %es:(%rcx), %rdx;
    mov %cs:(%rcx), %rdx;
    mov %ds:(%rcx), %rdx;
    mov %ss:(%rcx), %rdx;
*/
/*
    push   %rbp
    mov    %rsp,%rbp
    sub    $0x10,%rsp
    mov    %edi,-0x4(%rbp)
    call *%rax;
    mov    %rsi,-0x10(%rbp)
    mov    %rbp,%rsp
    pop    %rbp */
    /*
    mov (%rdi), %rsp;
    mov (%rdi), %rsp;
    mov (%rdi), %rsp;
    */
    /*

    movsw;
    retq */



    /*pushfw;
    popfw;
*/
   /* pushw $-1;
    pop %fs;

    push $-1;
*/
/*
  .Lfoo:
    push   %rbp
    mov    %rsp,%rbp
    sub    $0x10,%rsp
    mov    %edi,-0x4(%rbp)
    mov    %rsi,-0x10(%rbp)
    mov    -0x4(%rbp),%edi
    mov    -0x10(%rbp),%rsi
    jz .Lfoo;

    callq  *%rax
    callq  *%rdx
    mov    -0x10(%rbp),%rsi
    mov    (%rsi),%rdi
    callq  *%rcx
    mov    %rax,%rdi
    callq  *%rdi
    mov    $0x0,%eax
    add    $0x10,%rsp
    pop    %rbp
*/
    retq
/*
    push %rax;
    jnz granary_test_mangle;
    mov %rax, %rax;
    call (%rax);
    mov %rax, %rax;
    iret;

    call %rax;
    call %rax;
    call (%rax);

    ret;
*/
/*
    xlat;
    //push %rsp;
    push (%rsp);
    pop %rsp;
    //mov (%rax), %rax;
    //mov (%rdi,%rsi), %rsp;
    //mov (%rax), %rax;
    //movsq;

    adcq (%r15, %r14), %r13;
    addq (%r13, %r12), %r11;
    addq (%r11, %r10), %r9;
    addq (%r9, %r8), %rdi;
    addq (%rdi, %rsi), %rbp;
    addq (%rbp, %rbx), %rdx;
    addq (%rdx, %rcx), %rax;
    ret;
*/
/*
    push %rax;
    push %rbx;
    pop %rax;
    pop %rbx;

    mov 4(%rsi, %rdi, 2), %rdi;
    mov (%rdi, %rsi), %r14;
    mov (%rdi, %rsi), %r15;

    lea 8(%rdi, %rsi), %rdx;
    mov (%rdx), %rsi;

    lea (%rdi), %rsi;
    mov %rsi, %rdx;
    mov %rdx, (%rdx);
    add %rdx,  %rax;

    mov (%rsi, %rdi), %rsp;
    adcq $1, (%rdi);

    enter $10, $2;
    xlat;

//    ret;

    mov %rax, %rdi;
    mov %rdi, %rsi;
    mov (%rsi), %rdx;
    mov %rdi, (%rdi,%rdx);
    mov (%rdi, %rsi), %rdi;
   //xadd %rdi, %rsi;
    //movsq;
//    ret;

    mov %rsp, %rbp;
    sub $0x20, %rsp;

    mov %rsp, -0x8(%rbp);
    adcq $1, (%rsp);
    mov %rsp, (%rsp);
    mov %rsp, -0x8(%rsp);

    push (%rax);
    push (%rsp);
    push -0x8(%rsp);
    push -0x16(%rsp);

    lea -0x8(%rsp), %rax;
    mov %rax, 0x8(%rbp);
    //xlat;
    mov $0, %rax;
    callq .Lloop_call_through_stack;
    jmp .Lafter_loop_call_through_stack;

.Lloop_call_through_stack:
    mov %rsp, %rdi;
    cld;
    movsq;
    shr $1, %rdi;
    callq *-0x8(%rsp, %rdi, 2);
    cmp $0, %rax;

    jz .Lloop_call_through_stack;
    ret;

.Lafter_loop_call_through_stack:

    // Pop the stack frame. This introduces some interestingness: should the
    // next fragment be considered to be on an invalid frame, or a valid one?
    // Forward propagation would make it invalid, but back-propagation from
    // the ret would make it valid.
    mov %rbp, %rsp;

.Ldecrement_rax_until_zero_stack_unsafe:
    sub $1, %rax;
    jnz .Ldecrement_rax_until_zero_stack_unsafe;

    // In this block the stack pointer should be seen as valid because of
    // the `ret`.
    mov $1, %rax;
    leave;
    ret;
*/
#endif
END_FUNC(granary_test_mangle)

END_FILE
