/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/driver/xed2-intel64/instruction.h"
#include "granary/driver/xed2-intel64/relativize.h"

namespace granary {
namespace driver {

// Make a native instruction safe to execute from within the code cache.
// This sometimes results in additional instructions being
void InstructionRelativizer::Relativize(NativeInstruction *native_instr_) {
  native_instr = native_instr_;
  instr = native_instr->instruction.get();
  if (instr->has_pc_rel_op) {
    switch (instr->iclass) {
      case XED_ICLASS_LEA: return RelativizeLEA();
      case XED_ICLASS_PUSH: return RelativizePUSH();
      case XED_ICLASS_POP: return RelativizePOP();
      case XED_ICLASS_JMP:
      case XED_ICLASS_JMP_FAR:
      case XED_ICLASS_CALL_FAR:
      case XED_ICLASS_CALL_NEAR: return RelativizeCFI();
      default: return;
    }
  }
  GRANARY_UNUSED(cache_pc);
}

void InstructionRelativizer::RelativizeLEA(void) {}
void InstructionRelativizer::RelativizePUSH(void) {}
void InstructionRelativizer::RelativizePOP(void) {}
void InstructionRelativizer::RelativizeCFI(void) {}

}  // namespace driver
}  // namespace granary
