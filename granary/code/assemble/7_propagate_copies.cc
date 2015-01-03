/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/code/fragment.h"

#include "granary/code/assemble/7_propagate_copies.h"

#include "granary/breakpoint.h"
#include "granary/util.h"  // For `GetMetaData`.

namespace granary {

// TODO(pag):  I think a simpler way to do copy propagation will be to track
//             (somehow) that a register is defined once, and then only read
//             thereafter, but never used in an RW operation. Then, the entire
//             copy propagation can be symbolic. This would be akin to the
//             temporary registers of SUIF.

// Perform the following kinds of copy-propagation.
//    1) Register -> register.
//    2) Trivial effective address -> register.
//    3) Register -> base address of memory operand.
//    4) Effective address -> memory arch_operand.
//
// Returns true if anything was done.
bool PropagateRegisterCopies(FragmentList *) {
  return false;
}

}  // namespace granary
