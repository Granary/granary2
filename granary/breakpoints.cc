/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

extern "C" {

GRANARY_DISABLE_OPTIMIZER
extern void granary_break_on_fault(void) {
  GRANARY_INLINE_ASSEMBLY("");
  GRANARY_UNUSED(*static_cast<void *>(nullptr));
}

}

