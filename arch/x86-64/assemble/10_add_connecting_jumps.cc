/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/builder.h"

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

// Adds a fall-through jump, if needed, to this fragment.
NativeInstruction *AddFallThroughJump(Fragment *frag,
                                      Fragment *fall_through_frag) {
  auto label = new LabelInstruction;
  fall_through_frag->instrs.Prepend(label);

  arch::Instruction ni;
  JMP_RELBRd(&ni, label);
  auto instr = new BranchInstruction(&ni, label);
  frag->instrs.Append(instr);
  return instr;
}

// Returns true if the target of a jump must be encoded in a nearby location.
bool IsNearRelativeJump(NativeInstruction *instr) {
  switch (instr->instruction.iclass) {
    case XED_ICLASS_JRCXZ:
    case XED_ICLASS_LOOP:
    case XED_ICLASS_LOOPE:
    case XED_ICLASS_LOOPNE:
      return true;
    default:
      return false;
  }
}

}  // namespace arch
}  // namespace granary
