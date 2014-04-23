/* Copyright 2014 Peter Goodman, all rights reserved. */
#if 0
#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/instruction.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/operand.h"

#include "granary/code/register.h"


namespace granary {

// Returns true if this instruction is a copy instruction.
//
// Note: This has an architecture-specific implementation.
bool IsCopyInstruction(const NativeInstruction *instr) {
  const auto &instruction(instr->instruction);
  const auto iclass = instruction.iclass;
  if (XED_ICLASS_MOV == iclass) {
    return instruction.ops[1].IsRegister();
  } else {
    return XED_ICLASS_LEA == iclass;
  }
}

}  // namespace granary
#endif
