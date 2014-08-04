/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/select.h"

#include "granary/base/base.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

// Table to find the instruction selections for each iclass.
extern const xed_inst_t * const ICLASS_SELECTIONS[];
extern const xed_inst_t * const LAST_ICLASS_SELECTION;

namespace {

static bool MatchWidth(int bit_width, xed_operand_width_enum_t op_width) {
  if (-1 == bit_width) {
    return true;
  }
  switch (op_width) {
    case XED_OPERAND_WIDTH_MB:
    case XED_OPERAND_WIDTH_B:
      return 8 == bit_width;
    case XED_OPERAND_WIDTH_MEM16:
    case XED_OPERAND_WIDTH_MEM16INT:
    case XED_OPERAND_WIDTH_MW:
    case XED_OPERAND_WIDTH_W:
      return 16 == bit_width;
    case XED_OPERAND_WIDTH_MEM32INT:
    case XED_OPERAND_WIDTH_MD:
    case XED_OPERAND_WIDTH_D:
      return 32 == bit_width;
    case XED_OPERAND_WIDTH_M64INT:
    case XED_OPERAND_WIDTH_MQ:
    case XED_OPERAND_WIDTH_Q:
      return 64 == bit_width;
    default:
      asm("nop;");
      return true;
  }
}

// Try to match the type of an `arch::Operand` to the type of a `xed_inst_t`.
static bool MatchOperand(const Operand *instr_op,
                         const xed_operand_t *xedi_op) {
  auto op_name = xed_operand_name(xedi_op);
  switch (op_name) {
    case XED_OPERAND_IMM0SIGNED:
    case XED_OPERAND_IMM0:
    case XED_OPERAND_IMM1_BYTES:
    case XED_OPERAND_IMM1:
    case XED_OPERAND_RELBR:
      return instr_op->IsImmediate();
    case XED_OPERAND_MEM0:
    case XED_OPERAND_MEM1:
      if (!instr_op->IsMemory()) {
        return false;
      }
      return MatchWidth(instr_op->BitWidth(), xed_operand_width(xedi_op));
    case XED_OPERAND_REG0:
    case XED_OPERAND_REG1:
      if (!instr_op->IsRegister()) {
        return false;
      }
      return MatchWidth(instr_op->BitWidth(), xed_operand_width(xedi_op));
    default:
      GRANARY_ASSERT(false);
      return false;
  }
}

// Try to match the explicit operands of `instr` against the types of first
// operands of `xedi`.
bool MatchOperandTypes(const Instruction *instr, const xed_inst_t *xedi) {
  auto i = 0U;
  for (auto &instr_op : instr->ops) {
    if (XED_ENCODER_OPERAND_TYPE_INVALID == instr_op.type) {
      break;
    } else if (!MatchOperand(&instr_op, xed_inst_operand(xedi, i++))) {
      return false;
    }
  }
  return true;
}

}  // namespace

// Returns the `xed_inst_t` instance associated with this instruction. This
// won't necessarily return a perfect selection. That is, all that is required
// of the returned selection is that the types of the operands match
// (independent of the sizes of operands).
const xed_inst_t *SelectInstruction(const Instruction *instr) {
  for (auto xedi = ICLASS_SELECTIONS[instr->iclass];
      xedi < LAST_ICLASS_SELECTION; ++xedi) {
    auto iclass = xed_inst_iclass(xedi);
    if (iclass != instr->iclass) {
      return nullptr;
    } else if (MatchOperandTypes(instr, xedi)) {
      instr->iform = xed_inst_iform_enum(xedi);  // Update in-place.
      return xedi;
    }
  }
  return nullptr;
}

}  // namespace arch
}  // namespace granary
