/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_TRACKER_H_
#define GRANARY_CODE_TRACKER_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/base.h"
#include "granary/base/bitset.h"

namespace granary {
namespace driver {
// Forward declarations.
class Operand;
}

// A class that tracks live, general purpose architectural registers within a
// straight-line sequence of instructions.
class InstructionRegisterTracker {
 public:

 private:
  // Is the Nth register live on entry to this instruction?
  BitSet<arch::NUM_GENERAL_PURPOSE_REGISTERS> is_live;

  // Is the the Nth live register sticky? That is, is there any instruction in
  // the (block-local) live range of this register that *must* use this specific
  // architectural register?
  //
  // Stickiness here is defined in terms of:
  //    1) The register is used in an operand marked as sticky. I.e. this
  //       means that the GPR absolutely cannot be substituted with some other
  //       GPR or memory
  BitSet<arch::NUM_GENERAL_PURPOSE_REGISTERS> is_sticky;

  // Is the Nth register used in or after this instruction within the current
  // basic block?
  BitSet<arch::NUM_GENERAL_PURPOSE_REGISTERS> is_used;


};

}  // namespace granary

#endif  // GRANARY_CODE_TRACKER_H_
