/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/builder.h"

#include "granary/cfg/instruction.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {
namespace {
// Look for the pattern `XOR A, A`.
static bool IfClearedByXor(const Instruction &ni,
                           const Operand *op) {
  return op == &(ni.ops[0]) && ni.ops[0].reg == ni.ops[1].reg;
}

// Look for the pattern `SUB A, A`.
static bool IfClearedBySub(const Instruction &ni,
                                 const Operand *op) {
  return op == &(ni.ops[0]) && ni.ops[0].reg == ni.ops[1].reg;
}

// Look for the pattern `AND A, 0`.
static bool IfClearedByAnd(const Instruction &ni,
                           const Operand *op) {
  return op == &(ni.ops[0]) && !ni.ops[1].imm.as_uint;
}
}  // namespace

// Returns `true` if `op` in `instr`, which looks like a read/write operand,
// actually behaves like a write. This happens for things like:
//      SUB R, R
//      XOR R, R
//      AND R, 0
bool OperandIsWrite(const NativeInstruction *instr,
                    const granary::Operand *op_) {
  auto &ainstr(instr->instruction);
  auto op = op_->UnsafeExtract();
  switch (ainstr.iform) {
    case XED_IFORM_XOR_GPR8_GPR8_30:
    case XED_IFORM_XOR_GPR8_GPR8_32:
    case XED_IFORM_XOR_GPRv_GPRv_31:
    case XED_IFORM_XOR_GPRv_GPRv_33:
      return IfClearedByXor(ainstr, op);
    case XED_IFORM_SUB_GPR8_GPR8_28:
    case XED_IFORM_SUB_GPR8_GPR8_2A:
    case XED_IFORM_SUB_GPRv_GPRv_29:
    case XED_IFORM_SUB_GPRv_GPRv_2B:
      return IfClearedBySub(ainstr, op);
    case XED_IFORM_AND_GPR8_IMMb_80r4:
    case XED_IFORM_AND_GPR8_IMMb_82r4:
    case XED_IFORM_AND_GPRv_IMMb:
    case XED_IFORM_AND_GPRv_IMMz:
      return IfClearedByAnd(ainstr, op);
    default: return false;
  }
}

}  // namespace arch
}  // namespace granary
