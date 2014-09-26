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
granary::Instruction *SaveGPRToSlot(VirtualRegister gpr, VirtualRegister slot) {
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
granary::Instruction *RestoreGPRFromSlot(VirtualRegister gpr,
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
granary::Instruction *SwapGPRWithGPR(VirtualRegister gpr1,
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
granary::Instruction *SwapGPRWithSlot(VirtualRegister gpr,
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

namespace {

// Try to peephole optimize the following pattern that represents the filling of
// a spilled native register.
//      MOV_GPRv_GPRv_89/8B <native>, <spill>
//      MOV_GPRv_MEMv <spill>, [slot:N]
// Into:
//      MOV_GPRv_MEMv <native>, [slot:N]
static granary::Instruction *OptRestoreGPR(NativeInstruction *instr,
                                           NativeInstruction *next_instr) {
  auto &ainstr(instr->instruction);
  auto &next_ainstr(next_instr->instruction);
  if (next_ainstr.is_save_restore ||
      (XED_IFORM_MOV_GPRv_GPRv_89 != ainstr.iform &&
       XED_IFORM_MOV_GPRv_GPRv_8B != ainstr.iform) ||
      XED_IFORM_MOV_GPRv_MEMv != next_ainstr.iform ||
      !next_ainstr.ops[1].reg.IsVirtualSlot() ||
      ainstr.ops[1].reg != next_ainstr.ops[0].reg ||
      ainstr.ops[1].reg.ByteWidth() != next_ainstr.ops[0].reg.ByteWidth()) {
    return next_instr;
  }
  memcpy(&(next_ainstr.ops[0]), &(ainstr.ops[0]), sizeof ainstr.ops[0]);
  NOP_90(&ainstr);
  return next_instr->Next();
}

// Try to peephole optimize the following pattern that represents the spilling
// of a native register.
//      MOV_MEMv_GPRv [slot:N], <spill>
//      MOV_GPRv_GPRv_89/8B <spill>, <native>
// Into:
//      MOV_MEMv_GPRv [slot:N], <native>
static granary::Instruction *OptSaveGPR(NativeInstruction *instr,
                                        NativeInstruction *next_instr) {
  auto &ainstr(instr->instruction);
  auto &next_ainstr(next_instr->instruction);
  if (ainstr.is_save_restore ||
      XED_IFORM_MOV_MEMv_GPRv != ainstr.iform ||
      (XED_IFORM_MOV_GPRv_GPRv_89 != next_ainstr.iform &&
       XED_IFORM_MOV_GPRv_GPRv_8B != next_ainstr.iform) ||
      !ainstr.ops[0].reg.IsVirtualSlot() ||
      ainstr.ops[1].reg != next_ainstr.ops[0].reg ||
      ainstr.ops[1].reg.ByteWidth() != next_ainstr.ops[0].reg.ByteWidth()) {
    return next_instr;
  }
  memcpy(&(ainstr.ops[1]), &(next_ainstr.ops[1]), sizeof ainstr.ops[1]);
  NOP_90(&next_ainstr);
  return next_instr->Next();
}

// Returns the next instruction (either a label instruction or a native
// instruction, while allowing other annotation instructions to be skipped).
static granary::Instruction *NextInstruction(granary::Instruction *curr) {
  for (; curr; curr = curr->Next()) {
    if (IsA<LabelInstruction *>(curr)) return curr;
    if (IsA<NativeInstruction *>(curr)) return curr;
  }
  return nullptr;
}

}  // namespace

// Disable peephole optimization in a particular instruction.
void DisablePeepholeOptimization(NativeInstruction *instr) {
  instr->instruction.is_save_restore = false;
}

// Performs some minor peephole optimization on the scheduled registers.
void PeepholeOptimize(Fragment *frag) {
  auto instr(frag->instrs.First());
  granary::Instruction *next_instr(nullptr);
  for (; instr; instr = next_instr) {
    next_instr = NextInstruction(instr->Next());
    auto ninstr = DynamicCast<NativeInstruction *>(instr);
    if (!ninstr) continue;
    auto next_ninstr = DynamicCast<NativeInstruction *>(next_instr);
    if (!next_ninstr) continue;

    if (ninstr->instruction.is_save_restore) {
      next_instr = OptRestoreGPR(ninstr, next_ninstr);
    } else if (next_ninstr->instruction.is_save_restore) {
      next_instr = OptSaveGPR(ninstr, next_ninstr);
    }
  }
}

}  // namespace arch
}  // namespace granary
