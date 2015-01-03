/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/instruction.h"  // For `arch::Instruction`.

#include "granary/cfg/instruction.h"  // For `NativeInstruction`.

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

// Replace a memory operand with an effective address memory operand.
void ReplaceMemOpWithEffectiveAddress(Operand *mem_op,
                                      const Operand *effective_addr) {
  GRANARY_ASSERT(mem_op->IsMemory());
  GRANARY_ASSERT(effective_addr->IsMemory());
  GRANARY_ASSERT(effective_addr->IsEffectiveAddress());
  *mem_op = *effective_addr;
}

}  // namespace arch
}  // namespace granary
