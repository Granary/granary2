/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/instruction.h"
#include "granary/code/register.h"
#include "granary/breakpoint.h"

namespace granary {

// Union some other live register set with the current live register set.
// Returns true if there was a change in the set of live registers. This is
// useful when we want to be conservative about the potentially live
// registers out of a specific block.
bool RegisterTracker::Union(const RegisterTracker &that) {
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
bool RegisterTracker::Intersect(const RegisterTracker &that) {
  bool changed = false;
  for (size_t i = 0; i < STORAGE_LEN; ++i) {
    StorageT new_val = static_cast<StorageT>(storage[i] & that.storage[i]);
    changed = changed || new_val != storage[i];
    storage[i] = new_val;
  }
  return changed;
}

// Returns true if two register usage tracker sets are equivalent.
bool RegisterTracker::Equals(const RegisterTracker &that) const {
  for (size_t i = 0; i < STORAGE_LEN; ++i) {
    if (storage[i] != that.storage[i]) {
      return false;
    }
  }
  return true;
}

// Update this register tracker by visiting the operands of an instruction.
//
// Note: This treats conditional writes to a register as reviving that
//       register.
void LiveRegisterTracker::Visit(NativeInstruction *instr) {
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
        // Read, read/write, conditional write, or partial write.
        if (op->IsRead() || op->IsConditionalWrite() ||
            reg.PreservesBytesOnWrite()) {
          Revive(reg);
        } else if (op->IsWrite()) {  // Write-only.
          Kill(reg);
        }
      }
    }
  });
}

// Update this register tracker by visiting the operands of an instruction.
//
// Note: This treats conditional writes to a register as reviving that
//       register.
void DeadRegisterTracker::Visit(NativeInstruction *instr) {
  if (GRANARY_UNLIKELY(!instr)) {
    return;
  }
  // Treat conditional writes, read/writes, and partial writes as unconditional
  // writes. The idea is that what we really want to track is whether any part
  // of the register has potentially been modified.
  instr->ForEachOperand([=] (Operand *op) {
    if (auto rloc = DynamicCast<RegisterOperand *>(op)) {
      auto reg = rloc->Register();
      if (op->IsWrite() && reg.IsNative() && reg.IsGeneralPurpose()) {
        Kill(reg);  // Read/write, write, conditional write.
      }
    }
  });
  instr->ForEachOperand([=] (Operand *op) {
    // All registers participating in a memory operand are reads, because
    // they are used to compute the effective address of the memory operand.
    if (auto mloc = DynamicCast<MemoryOperand *>(op)) {
      VirtualRegister r1, r2, r3;
      mloc->CountMatchedRegisters({&r1, &r2, &r3});
      Revive(r1);
      Revive(r2);
      Revive(r3);

    // If this register operand doesn't write, then it's a read.
    } else if (auto rloc = DynamicCast<RegisterOperand *>(op)) {
      auto reg = rloc->Register();
      if (!op->IsWrite() && reg.IsNative() && reg.IsGeneralPurpose()) {
        Revive(reg);
      }
    }
  });
}

}  // namespace granary
