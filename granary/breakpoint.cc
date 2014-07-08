/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

#include "granary/breakpoint.h"

extern "C" {

GRANARY_DISABLE_OPTIMIZER
void granary_break_on_fault(void) {
  __builtin_trap();
}

GRANARY_DISABLE_OPTIMIZER
void granary_break_on_unreachable_code(void) {
  __builtin_trap();
}
}  // extern C
