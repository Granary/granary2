/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/arch/x86-64/asm/include.asm.inc"

START_FILE

// Get the user-space TLS base address.
//
// Note: This is Linux-specific, and assumes the first quadword of memory
//       pointed to by the base address of the segment descriptor for `FS`
//       is a pointer to said base address (i.e. self-reference).
DEFINE_FUNC(granary_arch_get_tls_base)
    movq %fs:0, %rax;
    retq;
END_FUNC(granary_arch_get_tls_base)

END_FILE
