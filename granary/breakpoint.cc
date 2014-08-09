/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

#include "granary/breakpoint.h"

extern "C" {

void granary_unreachable(void) {
  __builtin_trap();
}

}  // extern C
