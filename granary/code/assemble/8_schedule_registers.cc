/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/code/assemble/fragment.h"
#include "granary/code/assemble/ssa.h"

namespace granary {

// Schedule virtual registers to either physical registers or to stack/TLS
// slots.
void ScheduleRegisters(Fragment * const frags) {
  GRANARY_UNUSED(frags);
}

}  // namespace granary

