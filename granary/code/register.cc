/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"
#include "granary/code/register.h"
#include "granary/breakpoint.h"

namespace granary {

// Kill a specific register.
void RegisterSet::Kill(VirtualRegister reg) {
  if (reg.IsNative() && reg.IsGeneralPurpose()) {
    Kill(reg.Number());
  }
}

// Kill a specific register, where we treat this register is being part of
// a write. This takes into account the fact that two or more registers might
// alias the same data.
void RegisterSet::WriteKill(VirtualRegister reg) {
  if (reg.IsNative() && reg.IsGeneralPurpose()) {
    if (reg.PreservesBytesOnWrite()) {
      Revive(reg.Number());
    } else {
      Kill(reg.Number());
    }
  }
}

// Kill a specific register.
void RegisterSet::Revive(VirtualRegister reg) {
  if (reg.IsNative() && reg.IsGeneralPurpose()) {
    Revive(reg.Number());
  }
}

// Union some other live register set with the current live register set.
// Returns true if there was a change in the set of live registers. This is
// useful when we want to be conservative about the potentially live
// registers out of a specific block.
bool RegisterSet::Union(const RegisterSet &that) {
  bool changed = false;
  for (size_t i = 0; i < STORAGE_LEN; ++i) {
    StorageT new_val = static_cast<StorageT>(storage[i] | that.storage[i]);
    changed = changed || new_val != storage[i];
    storage[i] = new_val;
  }
  return changed;
}

// Intersect some other live register set with the current live register set.
// Returns true if there was a change in the set of live registers. This is
// useful when we want to be conservative about the potentially dead registers
// out of a specific block.
bool RegisterSet::Intersect(const RegisterSet &that) {
  bool changed = false;
  for (size_t i = 0; i < STORAGE_LEN; ++i) {
    StorageT new_val = static_cast<StorageT>(storage[i] & that.storage[i]);
    changed = changed || new_val != storage[i];
    storage[i] = new_val;
  }
  return changed;
}

// Returns true if two register usage tracker sets are equivalent.
bool RegisterSet::Equals(const RegisterSet &that) const {
  for (size_t i = 0; i < STORAGE_LEN; ++i) {
    if (storage[i] != that.storage[i]) {
      return false;
    }
  }
  return true;
}

// Overwrites one register usage tracker with another.
RegisterSet &RegisterSet::operator=(const RegisterSet &that) {
  if (this != &that) {
    this->Copy(that);
  }
  return *this;
}

// Update this register tracker by marking all registers that appear in an
// instruction as used.
void UsedRegisterSet::Visit(const NativeInstruction *instr) {
  Visit(&(instr->instruction));
}

// Update this register tracker by marking some registers as used (i.e.
// restricted). This allows us to communicate some architecture-specific
// encoding constraints to the register scheduler.
void UsedRegisterSet::ReviveRestrictedRegisters(
    const NativeInstruction *instr) {
  ReviveRestrictedRegisters(&(instr->instruction));
}

// Update this register tracker by visiting the operands of an instruction.
void LiveRegisterSet::Visit(const NativeInstruction *instr) {
  Visit(&(instr->instruction));
}

}  // namespace granary
