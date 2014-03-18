/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/code/register.h"

namespace granary {

// Initialize the register tracker.
RegisterUsageTracker::RegisterUsageTracker(void) {
  ReviveAll();
}

namespace {
// Get a virtual register out of an operand.
VirtualRegister GetRegister(const Operand *op) {
  VirtualRegister vr;
  if (auto reg = DynamicCast<RegisterOperand *>(op)) {
    vr = reg->Register();
  } else if (auto mem = DynamicCast<MemoryOperand *>(op)) {
    mem->MatchRegister(vr);
  }
  return vr;
}
}  // namespace

// Update this register tracker by visiting the operands of an instruction.
void RegisterUsageTracker::Visit(NativeInstruction *instr) {
  instr->ForEachOperand([&] (Operand *op) {
    auto reg = GetRegister(op);
    if (reg.IsNative()) {
      auto num = reg.Number();
      if (op->IsRead() || op->IsConditionalWrite()) {
        Revive(num);  // Read, read/write, and conditional write.
      } else if (op->IsWrite()) {  // Write-only.
        Set(static_cast<unsigned>(num), reg.PreservesBytesOnWrite());
      }
    }
  });
}

// Union some other live register set with the current live register set.
// Returns true if there was a change in the set of live registers.
bool RegisterUsageTracker::Union(const RegisterUsageTracker &that) {
  bool changed = false;
  for (size_t i = 0; i < sizeof storage; ++i) {
    uint8_t new_byte = storage[i] | that.storage[i];
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
