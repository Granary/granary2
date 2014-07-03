/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/instruction.h"

#include "granary/cfg/instruction.h"

#include "granary/code/ssa.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {
namespace {

// Look for the pattern `XOR A, A`.
static void UpdateIfClearedByXor(const Operand *arch_ops,
                                 SSAOperandPack &ssa_ops) {
  if (!arch_ops[0].IsRegister()) return;
  if (!arch_ops[1].IsRegister()) return;
  if (arch_ops[0].reg != arch_ops[1].reg) {
    ssa_ops[0].action = SSAOperandAction::READ_WRITE;
    ssa_ops[1].action = SSAOperandAction::READ;
  } else {
    ssa_ops[0].action = SSAOperandAction::WRITE;
    ssa_ops[1].action = SSAOperandAction::CLEARED;
  }
}

// Look for the pattern `SUB A, A`.
static void UpdateIfClearedBySub(const Operand *arch_ops,
                                 SSAOperandPack &ssa_ops) {
  if (!arch_ops[0].IsRegister()) return;
  if (!arch_ops[1].IsRegister()) return;
  if (arch_ops[0].reg == arch_ops[1].reg) {
    ssa_ops[0].action = SSAOperandAction::WRITE;
    ssa_ops[1].action = SSAOperandAction::CLEARED;
  }
}

// Look for the pattern `AND A, 0`.
static void UpdateIfClearedByAnd(const Operand *arch_ops,
                                 SSAOperandPack &ssa_ops) {
  if (!arch_ops[0].IsRegister()) return;
  if (!arch_ops[1].IsImmediate()) return;
  if (0 == arch_ops[1].imm.as_uint &&
      !arch_ops[0].reg.PreservesBytesOnWrite()) {
    ssa_ops[0].action = SSAOperandAction::WRITE;
  }
}

}  // namespace

// Performs architecture-specific conversion of `SSAOperand` actions. The things
// we want to handle here are instructions like `XOR A, A`, that can be seen as
// clearing the value of `A` and not reading it for the sake of reading it.
void ConvertOperandActions(const NativeInstruction *instr,
                           SSAOperandPack &operands) {
  auto &ainstr(instr->instruction);
  switch (ainstr.iclass) {
    case XED_ICLASS_XOR:
      UpdateIfClearedByXor(ainstr.ops, operands);
      break;
    case XED_ICLASS_SUB:
      UpdateIfClearedBySub(ainstr.ops, operands);
      break;
    case XED_ICLASS_AND:
      UpdateIfClearedByAnd(ainstr.ops, operands);
      break;
    default: return;
  }
}

// Get the virtual register associated with an arch operand.
//
// Note: This assumes that the arch operand is indeed a register operand!
VirtualRegister GetRegister(const SSAOperand &op) {
  GRANARY_ASSERT(op.operand->IsRegister());
  return op.operand->reg;
}

}  // namespace arch
}  // namespace granary
