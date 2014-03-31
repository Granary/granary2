/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/instruction.h"
#include "granary/code/register.h"
#include "granary/breakpoint.h"

namespace granary {

// Initialize the register tracker.
RegisterUsageTracker::RegisterUsageTracker(void) {
  ReviveAll();
}

// Update this register tracker by visiting the operands of an instruction.
void RegisterUsageTracker::Visit(NativeInstruction *instr) {
  if (GRANARY_UNLIKELY(!instr)) {
    return;
  }
  instr->ForEachOperand([=] (Operand *op) {
    // All registers participating in a memory operand are reads, because
    // they are used to compute the effective address of the memory operand.
    if (auto mloc = DynamicCast<MemoryOperand *>(op)) {
      VirtualRegister r1, r2, r3;
      mloc->CountMatchedRegisters({&r1, &r2, &r3});
      Revive(r1);
      Revive(r2);
      Revive(r3);
    } else if (auto rloc = DynamicCast<RegisterOperand *>(op)) {
      auto reg = rloc->Register();
      if (reg.IsNative() && reg.IsGeneralPurpose()) {
        if (op->IsRead() || op->IsConditionalWrite()) {
          Revive(reg);  // Read, read/write, and conditional write.
        } else if (op->IsWrite()) {  // Write-only.
          WriteKill(reg);
        }
      }
    }
  });
}

// Union some other live register set with the current live register set.
// Returns true if there was a change in the set of live registers. This is
// useful when we want to be conservative about the potentially live
// registers out of a specific block.
bool RegisterUsageTracker::Union(const RegisterUsageTracker &that) {
  bool changed = false;
  for (size_t i = 0; i < sizeof storage; ++i) {
    uint8_t new_byte = storage[i] | that.storage[i];
    changed = changed || new_byte != storage[i];
    storage[i] = new_byte;
  }
  return changed;
}

// Intersect some other live register set with the current live register set.
// Returns true if there was a change in the set of live registers. This is
// useful when we want to be conservative about the potentially dead registers
// out of a specific block.
bool RegisterUsageTracker::Intersect(const RegisterUsageTracker &that) {
  bool changed = false;
  for (size_t i = 0; i < sizeof storage; ++i) {
    uint8_t new_byte = storage[i] & that.storage[i];
    changed = changed || new_byte != storage[i];
    storage[i] = new_byte;
  }
  return changed;
}

// Returns true if two register usage tracker sets are equivalent.
bool RegisterUsageTracker::Equals(const RegisterUsageTracker &that) const {
  for (size_t i = 0; i < sizeof storage; ++i) {
    if (storage[i] != that.storage[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace granary
