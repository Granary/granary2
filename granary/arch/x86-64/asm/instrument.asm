/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/arch/x86-64/asm/include.asm.inc"

START_FILE

.extern _ZN7granary6detail10InstrumentEPPKh

// void granary::Instrument(void);
DEFINE_FUNC(_ZN7granary10InstrumentEv)
    lea (%rsp), %rdi;
    lea _ZN7granary6detail10InstrumentEPPKh(%rip), %rax;
    jmpq *%rax;
    ret;
END_FUNC(_ZN7granary10InstrumentEv)

END_FILE

