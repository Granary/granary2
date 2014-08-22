/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

#include "granary/breakpoint.h"

extern "C" {

void granary_unreachable(void) {
  GRANARY_IF_VALGRIND(VALGRIND_PRINTF_BACKTRACE("Assertion failed:\n"));
  __builtin_trap();
}

void granary_curiosity(void) {
  GRANARY_INLINE_ASSEMBLY("" ::: "memory");
}

}  // extern C
