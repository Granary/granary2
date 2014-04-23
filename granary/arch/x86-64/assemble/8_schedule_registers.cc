/* Copyright 2014 Peter Goodman, all rights reserved. */
#if 0
#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/builder.h"
#include "granary/arch/x86-64/select.h"

#include "granary/base/base.h"
#include "granary/base/cast.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/operand.h"

#include "granary/breakpoint.h"

namespace granary {

// Speculates on whether or not a particular instruction selection exists for
// some set of explicit operands. Returns true if we thing the selection does
// exist.
//
// This instruction is a bit sketchy. Copies the original arch instruction
// associated with `ninstr`, then figures out where in the instruction `op` is,
// then points `repl_op` at the associated operand (based on the computed
// offset) in the copied instruction, then does the replacement, then searches
// for an instruction selection for the instruction.
bool TryReplaceOperand(const NativeInstruction *ninstr,
                       const Operand *op, Operand *repl_op) {
  GRANARY_ASSERT(op->Ref().IsValid());

  auto orig_instr = &(ninstr->instruction);
  auto orig_instr_op = op->op_ptr;
  auto instr = *orig_instr;
  auto offset = reinterpret_cast<uintptr_t>(orig_instr_op) -
                reinterpret_cast<uintptr_t>(orig_instr);
  auto old_repl_op_op_ptr = repl_op->op_ptr;
  repl_op->op_ptr = UnsafeCast<arch::Operand *>(
      UnsafeCast<char *>(&instr) + offset);

  repl_op->Ref().ReplaceWith(*repl_op);
  auto can_replace = SelectInstruction(&instr);
  repl_op->op_ptr = old_repl_op_op_ptr;

  return can_replace;
}

// Create an instruction to copy a GPR to a spill slot.
std::unique_ptr<Instruction> SaveGPRToSlot(VirtualRegister gpr,
                                           VirtualRegister slot) {
  GRANARY_ASSERT(gpr.IsNative());
  GRANARY_ASSERT(slot.IsVirtualSlot());
  arch::Instruction ninstr;
  gpr.Widen(arch::GPR_WIDTH_BYTES);
  slot.Widen(arch::ADDRESS_WIDTH_BYTES);
  arch::MOV_MEMv_GPRv(&ninstr, slot, gpr);
  return std::unique_ptr<Instruction>(new NativeInstruction(&ninstr));
}

// Create an instruction to copy the value of a spill slot to a GPR.
std::unique_ptr<Instruction> RestoreGPRFromSlot(VirtualRegister gpr,
                                                VirtualRegister slot) {
  GRANARY_ASSERT(gpr.IsNative());
  GRANARY_ASSERT(slot.IsVirtualSlot());
  arch::Instruction ninstr;
  gpr.Widen(arch::GPR_WIDTH_BYTES);
  slot.Widen(arch::ADDRESS_WIDTH_BYTES);
  arch::MOV_GPRv_MEMv(&ninstr, gpr, slot);
  return std::unique_ptr<Instruction>(new NativeInstruction(&ninstr));
}

}  // namespace granary
#endif
