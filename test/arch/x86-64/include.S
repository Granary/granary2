/* Copyright 2014 Peter Goodman, all rights reserved. */

#define BEGIN_FUNC(name) \
    .align 16; \
    .globl name; \
    .type name, @function; \
    name: \
    .cfi_startproc; \

#define END_FUNC \
    .cfi_endproc;

#define UINT64(name, val) \
    .align 8; \
    .globl name; \
    .type name, @object; \
    name: \
    .quad val;
