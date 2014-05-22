/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/builder.h"

#include "granary/base/base.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/operand.h"

#include "granary/breakpoint.h"

namespace granary {

// Create an instruction to copy a GPR to a spill slot.
Instruction *SaveGPRToSlot(VirtualRegister gpr, VirtualRegister slot) {
  GRANARY_ASSERT(gpr.IsNative());
  GRANARY_ASSERT(slot.IsVirtualSlot());
  arch::Instruction ninstr;
  gpr.Widen(arch::GPR_WIDTH_BYTES);
  slot.Widen(arch::ADDRESS_WIDTH_BYTES);
  arch::MOV_MEMv_GPRv(&ninstr, slot, gpr);
  return new NativeInstruction(&ninstr);
}

// Create an instruction to copy the value of a spill slot to a GPR.
Instruction *RestoreGPRFromSlot(VirtualRegister gpr, VirtualRegister slot) {
  GRANARY_ASSERT(gpr.IsNative());
  GRANARY_ASSERT(slot.IsVirtualSlot());
  arch::Instruction ninstr;
  gpr.Widen(arch::GPR_WIDTH_BYTES);
  slot.Widen(arch::ADDRESS_WIDTH_BYTES);
  arch::MOV_GPRv_MEMv(&ninstr, gpr, slot);
  return new NativeInstruction(&ninstr);
}

// Swaps the value of one GPR with another.
//
// Note: This has an architecture-specific implementation.
Instruction *SwapGPRWithGPR(VirtualRegister gpr1, VirtualRegister gpr2) {
  GRANARY_ASSERT(gpr1.IsNative());
  GRANARY_ASSERT(gpr2.IsNative());
  arch::Instruction ninstr;
  gpr1.Widen(arch::GPR_WIDTH_BYTES);
  gpr2.Widen(arch::GPR_WIDTH_BYTES);
  arch::XCHG_GPRv_GPRv(&ninstr, gpr1, gpr2);
  return new NativeInstruction(&ninstr);
}

// Swaps the value of one GPR with a slot.
//
// Note: This has an architecture-specific implementation.
Instruction *SwapGPRWithSlot(VirtualRegister gpr, VirtualRegister slot) {
  GRANARY_ASSERT(gpr.IsNative());
  GRANARY_ASSERT(slot.IsVirtualSlot());
  arch::Instruction ninstr;
  gpr.Widen(arch::GPR_WIDTH_BYTES);
  slot.Widen(arch::ADDRESS_WIDTH_BYTES);
  arch::XCHG_MEMv_GPRv(&ninstr, slot, gpr);
  return new NativeInstruction(&ninstr);
}

// Returns the GPR that is copied by this instruction into a virtual
// register. If this instruction is not a simple copy operation of this form,
// then an invalid virtual register is returned.
VirtualRegister GPRCopiedToVR(const NativeInstruction *instr) {
  const auto &ainstr(instr->instruction);
  if (XED_ICLASS_MOV == ainstr.iclass) {
    if (ainstr.ops[0].IsRegister() && ainstr.ops[0].reg.IsVirtual() &&
        ainstr.ops[1].IsRegister() && ainstr.ops[1].reg.IsNative() &&
        ainstr.ops[1].reg.IsGeneralPurpose() &&
        arch::GPR_WIDTH_BYTES == ainstr.ops[0].reg.ByteWidth() &&
        arch::GPR_WIDTH_BYTES == ainstr.ops[1].reg.ByteWidth()) {
      return ainstr.ops[1].reg;
    }
  } else if (XED_ICLASS_LEA == ainstr.iclass) {
    if (ainstr.ops[0].IsRegister() && ainstr.ops[0].reg.IsVirtual() &&
        !ainstr.ops[1].is_compound && ainstr.ops[1].reg.IsNative() &&
        ainstr.ops[1].reg.IsGeneralPurpose() &&
        arch::GPR_WIDTH_BYTES == ainstr.ops[0].reg.ByteWidth() &&
        arch::GPR_WIDTH_BYTES == ainstr.ops[1].reg.ByteWidth()) {
      return ainstr.ops[1].reg;
    }
  }
  return VirtualRegister();
}

// Returns the GPR that is copied by this instruction from a virtual
// register. If this instruction is not a simple copy operation of this form,
// then an invalid virtual register is returned.
VirtualRegister GPRCopiedFromVR(const NativeInstruction *instr) {
  const auto &ainstr(instr->instruction);
  if (XED_ICLASS_MOV == ainstr.iclass) {
    if (ainstr.ops[0].IsRegister() && ainstr.ops[0].reg.IsNative() &&
        ainstr.ops[0].reg.IsGeneralPurpose() &&
        ainstr.ops[1].IsRegister() && ainstr.ops[1].reg.IsVirtual() &&
        arch::GPR_WIDTH_BYTES == ainstr.ops[0].reg.ByteWidth() &&
        arch::GPR_WIDTH_BYTES == ainstr.ops[1].reg.ByteWidth()) {
      return ainstr.ops[0].reg;
    }
  } else if (XED_ICLASS_LEA == ainstr.iclass) {
    if (ainstr.ops[0].IsRegister() && ainstr.ops[0].reg.IsNative() &&
        ainstr.ops[0].reg.IsGeneralPurpose() &&
        !ainstr.ops[1].is_compound && ainstr.ops[1].reg.IsVirtual() &&
        arch::GPR_WIDTH_BYTES == ainstr.ops[0].reg.ByteWidth() &&
        arch::GPR_WIDTH_BYTES == ainstr.ops[1].reg.ByteWidth()) {
      return ainstr.ops[0].reg;
    }
  }
  return VirtualRegister();
}

}  // namespace granary
