/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/instruction.h"

namespace granary {

// Returns true if the instruction modifies the stack pointer by a constant
// value, otherwise returns false.
//
// Note: This function assumes that the stack pointer is an operand of the
//       input instruction, and that it is a destination operand.
bool IsConstantStackPointerChange(const arch::Instruction &instr) {
  switch (instr.iclass) {
    case XED_ICLASS_PUSH:
    case XED_ICLASS_POP:
    case XED_ICLASS_CALL_NEAR:
    case XED_ICLASS_CALL_FAR:
    case XED_ICLASS_RET_NEAR:
    case XED_ICLASS_RET_FAR:
    case XED_ICLASS_ENTER:
      return true;

    case XED_ICLASS_MOV:
    case XED_ICLASS_ADD:
    case XED_ICLASS_SUB:
      return instr.ops[1].IsImmediate();

    case XED_ICLASS_LEA:
      return instr.ops[1].IsMemory() && instr.ops[1].is_compound &&
             XED_REG_RSP == instr.ops[1].mem.reg_base &&
             XED_REG_INVALID == instr.ops[1].mem.reg_index;

    default:
      return false;
  }
}

}  // namespace granary
