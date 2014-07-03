/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/builder.h"

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {
namespace {

static LabelInstruction *FindLabel(Fragment *frag) {
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto label = DynamicCast<LabelInstruction *>(instr)) {
      return label;
    }
  }
  auto label = new LabelInstruction;
  frag->instrs.Append(label);
  return label;
}

}  // namespace

// Adds a fall-through jump, if needed, to this fragment.
NativeInstruction *AddFallThroughJump(Fragment *frag,
                                      Fragment *fall_through_frag) {
  arch::Instruction ni;
  JMP_RELBRd(&ni, nullptr);
  auto instr = new BranchInstruction(&ni, FindLabel(fall_through_frag));
  frag->instrs.Append(instr);
  return instr;
}

}  // namespace arch
}  // namespace granary
