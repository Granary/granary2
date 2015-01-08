/* Copyright 2014 Peter Goodman, all rights reserved. */

#define BEGIN_INST_FUNC(name) \
    .section .text.inst_exports; \
    .align 16; \
    .globl name; \
    .type name, @function; \
    name: \
    .cfi_startproc;

#define BEGIN_TEST_FUNC(name) \
    .section .text.test_cases; \
    .align 16; \
    .globl name; \
    .type name, @function; \
    name: \
    .cfi_startproc;

#define END_FUNC \
    .cfi_endproc;

#define UINT64(name, val) \
    .data; \
    .align 8; \
    .globl name; \
    .type name, @object; \
    name: \
    .quad val;
