/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/builder.h"

#include "granary/base/base.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/operand.h"

#include "granary/code/fragment.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

// Create an instruction to copy a GPR to a spill slot.
NativeInstruction *SaveGPRToSlot(VirtualRegister gpr,
                                 VirtualRegister slot) {
  GRANARY_ASSERT(gpr.IsNative());
  GRANARY_ASSERT(slot.IsVirtualSlot());
  arch::Instruction ninstr;
  gpr.Widen(arch::GPR_WIDTH_BYTES);
  slot.Widen(arch::ADDRESS_WIDTH_BYTES);
  arch::MOV_MEMv_GPRv(&ninstr, slot, gpr);
  ninstr.ops[0].width = arch::GPR_WIDTH_BITS;
  return new NativeInstruction(&ninstr);
}

// Create an instruction to copy the value of a spill slot to a GPR.
NativeInstruction *RestoreGPRFromSlot(VirtualRegister gpr,
                                      VirtualRegister slot) {
  GRANARY_ASSERT(gpr.IsNative());
  GRANARY_ASSERT(slot.IsVirtualSlot());
  arch::Instruction ninstr;
  gpr.Widen(arch::GPR_WIDTH_BYTES);
  slot.Widen(arch::ADDRESS_WIDTH_BYTES);
  arch::MOV_GPRv_MEMv(&ninstr, gpr, slot);
  ninstr.ops[1].width = arch::GPR_WIDTH_BITS;
  return new NativeInstruction(&ninstr);
}

// Swaps the value of one GPR with another.
NativeInstruction *SwapGPRWithGPR(VirtualRegister gpr1,
                                  VirtualRegister gpr2) {
  GRANARY_ASSERT(gpr1.IsNative());
  GRANARY_ASSERT(gpr2.IsNative());
  arch::Instruction ninstr;
  gpr1.Widen(arch::GPR_WIDTH_BYTES);
  gpr2.Widen(arch::GPR_WIDTH_BYTES);
  arch::XCHG_GPRv_GPRv(&ninstr, gpr1, gpr2);
  return new NativeInstruction(&ninstr);
}

// Swaps the value of one GPR with a slot.
NativeInstruction *SwapGPRWithSlot(VirtualRegister gpr,
                                   VirtualRegister slot) {
  GRANARY_ASSERT(gpr.IsNative());
  GRANARY_ASSERT(slot.IsVirtualSlot());
  arch::Instruction ninstr;
  gpr.Widen(arch::GPR_WIDTH_BYTES);
  slot.Widen(arch::ADDRESS_WIDTH_BYTES);
  arch::XCHG_MEMv_GPRv(&ninstr, slot, gpr);
  ninstr.ops[0].width = arch::GPR_WIDTH_BITS;
  return new NativeInstruction(&ninstr);
}

}  // namespace arch
}  // namespace granary
