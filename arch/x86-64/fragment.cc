/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/instruction.h"
#include "arch/x86-64/register.h"

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"

namespace granary {

namespace {
static void CountGPRUse(RegisterUsageCounter *counter, VirtualRegister reg) {
  if (reg.IsNative() && reg.IsGeneralPurpose()) {
    counter->num_uses_of_gpr[reg.Number()] += 1;
  }
}
}  // namespace

// Count the number of uses of the arch GPRs in a particular instruction.
void RegisterUsageCounter::CountGPRUses(const NativeInstruction *instr) {
  auto &ainstr(instr->instruction);
  for (auto &aop : ainstr.ops) {
    if (aop.IsRegister()) {
      CountGPRUse(this, aop.reg);
    } else if (aop.IsMemory() && !aop.IsPointer()) {
      if (aop.is_compound) {
        CountGPRUse(this, aop.mem.base);
        CountGPRUse(this, aop.mem.index);
      } else {
        CountGPRUse(this, aop.reg);
      }
    } else if (!aop.IsValid()) {
      return;
    }
  }
}

}  // namespace granary