/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/builder.h"

#include "granary/cfg/instruction.h"

#include "granary/code/ssa.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {
namespace {

// Look for the pattern `XOR A, A`.
static void UpdateIfClearedByXor(const Operand *arch_ops,
                                 SSAInstruction *instr) {
  if (arch_ops[0].reg != arch_ops[1].reg) {
    instr->ops[0].action = SSAOperandAction::kSSAOperandActionReadWrite;
    instr->ops[1].action = SSAOperandAction::kSSAOperandActionRead;
  } else {
    instr->ops[0].action = SSAOperandAction::kSSAOperandActionWrite;
    instr->ops[1].action = SSAOperandAction::kSSAOperandActionCleared;
  }
}

// Look for the pattern `SUB A, A`.
static void UpdateIfClearedBySub(const Operand *arch_ops,
                                 SSAInstruction *instr) {
  if (arch_ops[0].reg == arch_ops[1].reg) {
    instr->ops[0].action = SSAOperandAction::kSSAOperandActionWrite;
    instr->ops[1].action = SSAOperandAction::kSSAOperandActionCleared;
  }
}

// Look for the pattern `AND A, 0`.
static void UpdateIfClearedByAnd(const Operand *arch_ops,
                                 SSAInstruction *instr) {
  if (0 == arch_ops[1].imm.as_uint &&
      !arch_ops[0].reg.PreservesBytesOnWrite()) {
    instr->ops[0].action = SSAOperandAction::kSSAOperandActionWrite;
  }
}

// Look for things like `MOV R, R` and either elide them, or modify the
// source register appropriately.
static void UpdateRegCopy(Instruction *ni, SSAInstruction *instr) {
  auto dst_reg = ni->ops[0].reg;
  auto src_reg = ni->ops[1].reg;

  // TODO(pag): What if `true == ni->dont_encode` from a previous step? For
  //            example:
  //                  step 0:
  //                        MOV A, B
  //                        MOV A, A    <dont_encode>
  //                        ..
  //                  step 1:
  //                        MOV A, B
  //                        MOV A, B    <dont_encode>
  if (dst_reg != src_reg) return;

  auto &ssa_op0(instr->ops[0]);
  auto &ssa_op1(instr->ops[1]);
  ssa_op0.action = SSAOperandAction::kSSAOperandActionReadWrite;
  ssa_op0.reg_web.Union(ssa_op1.reg_web);

  if (dst_reg.BitWidth() != src_reg.BitWidth() ||
      dst_reg.ByteWidth() != dst_reg.EffectiveWriteWidth()) {
    return;
  }

  // This is effectively a no-op.
  ni->DontEncode();
}

// Look for the pattern `LEA R, [R]` and make sure that the destination operand
// is treated as a READ_WRITE.
static void UpdateEffectiveAddress(Instruction *ni, SSAInstruction *instr) {
  GRANARY_ASSERT(2 == ni->num_explicit_ops);
  if (ni->ops[1].is_compound) return;
  if (ni->ops[1].IsPointer()) return;

  auto dst_reg = ni->ops[0].reg;
  auto src_reg = ni->ops[1].reg;

  // `LEA A, [B]`.
  if (dst_reg != src_reg) {

    // Maintain the invariant that a singleton stack pointer still needs to
    // remain as an LEA so that it is an effective address.
    if (!src_reg.IsStackPointer()) {
      MOV_GPRv_GPRv_89(ni, dst_reg, src_reg);
      instr->ops[0].action = SSAOperandAction::kSSAOperandActionWrite;
      instr->ops[1].action = SSAOperandAction::kSSAOperandActionRead;
    }

  // `LEA R, [R]`.
  } else {
    auto &ssa_op0(instr->ops[0]);
    auto &ssa_op1(instr->ops[1]);
    MOV_GPRv_GPRv_89(ni, dst_reg, dst_reg);
    ni->DontEncode();  // This instruction is useless.
    ssa_op0.action = SSAOperandAction::kSSAOperandActionReadWrite;
    ssa_op1.action = SSAOperandAction::kSSAOperandActionRead;
    ssa_op0.reg_web.Union(ssa_op1.reg_web);
  }
}

}  // namespace

// Performs architecture-specific conversion of `SSAOperand` actions. The things
// we want to handle here are instructions like `XOR A, A`, that can be seen as
// clearing the value of `A` and not reading it for the sake of reading it.
void ConvertOperandActions(NativeInstruction *instr) {
  auto &ainstr(instr->instruction);
  auto ssa_instr = instr->ssa;

  switch (ainstr.iform) {
    case XED_IFORM_XOR_GPR8_GPR8_30:
    case XED_IFORM_XOR_GPR8_GPR8_32:
    case XED_IFORM_XOR_GPRv_GPRv_31:
    case XED_IFORM_XOR_GPRv_GPRv_33:
      UpdateIfClearedByXor(ainstr.ops, ssa_instr); return;
    case XED_IFORM_SUB_GPR8_GPR8_28:
    case XED_IFORM_SUB_GPR8_GPR8_2A:
    case XED_IFORM_SUB_GPRv_GPRv_29:
    case XED_IFORM_SUB_GPRv_GPRv_2B:
      UpdateIfClearedBySub(ainstr.ops, ssa_instr); return;
    case XED_IFORM_AND_GPR8_IMMb_80r4:
    case XED_IFORM_AND_GPR8_IMMb_82r4:
    case XED_IFORM_AND_GPRv_IMMb:
    case XED_IFORM_AND_GPRv_IMMz:
      UpdateIfClearedByAnd(ainstr.ops, ssa_instr); return;
    case XED_IFORM_MOV_GPR8_GPR8_88:
    case XED_IFORM_MOV_GPR8_GPR8_8A:
    case XED_IFORM_MOV_GPRv_GPRv_89:
    case XED_IFORM_MOV_GPRv_GPRv_8B:
      UpdateRegCopy(&ainstr, ssa_instr); return;
    case XED_IFORM_LEA_GPRv_AGEN:
      UpdateEffectiveAddress(&ainstr, ssa_instr); return;
    default: return;
  }
}

// Invalidates the stack analysis property of `instr`.
void InvalidateStackAnalysis(NativeInstruction *instr) {
  if (instr->instruction.analyzed_stack_usage) {
    instr->instruction.analyzed_stack_usage = false;
    instr->instruction.reads_from_stack_pointer = false;
    instr->instruction.writes_to_stack_pointer = false;
  }
}

}  // namespace arch
}  // namespace granary
