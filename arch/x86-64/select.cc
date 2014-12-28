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

static bool MatchWidth(size_t bit_width, xed_operand_width_enum_t op_width) {
  if (!bit_width) return true;
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
      return true;
  }
}

// Returns true if we're able to match the exact register in a Granary operand
// with the register in a XED operand.
static bool MatchExactReg(VirtualRegister vreg, const xed_operand_t *op) {
  auto op_type = xed_operand_type(op);
  const xed_reg_enum_t reg = static_cast<xed_reg_enum_t>(vreg.EncodeToNative());

  if (XED_OPERAND_TYPE_NT_LOOKUP_FN == op_type) {
    switch (xed_operand_nonterminal_name(op)) {
      case XED_NONTERMINAL_AR10: return XED_REG_R10 == reg;
      case XED_NONTERMINAL_AR11: return XED_REG_R11 == reg;
      case XED_NONTERMINAL_AR12: return XED_REG_R12 == reg;
      case XED_NONTERMINAL_AR13: return XED_REG_R13 == reg;
      case XED_NONTERMINAL_AR14: return XED_REG_R14 == reg;
      case XED_NONTERMINAL_AR15: return XED_REG_R15 == reg;
      case XED_NONTERMINAL_AR8: return XED_REG_R8 == reg;
      case XED_NONTERMINAL_AR9: return XED_REG_R9 == reg;
      case XED_NONTERMINAL_ARAX: return XED_REG_RAX == reg;
      case XED_NONTERMINAL_ARBP: return XED_REG_RBP == reg;
      case XED_NONTERMINAL_ARBX: return XED_REG_RBX == reg;
      case XED_NONTERMINAL_ARCX: return XED_REG_RCX == reg;
      case XED_NONTERMINAL_ARDI: return XED_REG_RDI == reg;
      case XED_NONTERMINAL_ARDX: return XED_REG_RDX == reg;
      case XED_NONTERMINAL_ARSI: return XED_REG_RSI == reg;
      case XED_NONTERMINAL_ARSP: return XED_REG_RSP == reg;
      case XED_NONTERMINAL_OEAX: return XED_REG_EAX == reg;
      case XED_NONTERMINAL_ORAX: return XED_REG_RAX == reg;
      case XED_NONTERMINAL_ORBP: return XED_REG_RBP == reg;
      case XED_NONTERMINAL_ORDX: return XED_REG_RDX == reg;
      case XED_NONTERMINAL_ORSP: return XED_REG_RSP == reg;
      case XED_NONTERMINAL_RIP: return XED_REG_RIP == reg;
      case XED_NONTERMINAL_SRBP: return XED_REG_RBP == reg;
      case XED_NONTERMINAL_SRSP: return XED_REG_RSP == reg;
      case XED_NONTERMINAL_RFLAGS: return XED_REG_RFLAGS == reg;
      default: return false;
    }
  // Hard-coded register.
  } else if (XED_OPERAND_TYPE_REG == op_type) {
    return xed_operand_reg(op) == reg;

  } else {
    return false;
  }
}

enum {
  kBadSelectionScore = -2 * Instruction::MAX_NUM_OPERANDS,
  kGoodSelectionScore = 1,
  kGreatSelectionScore = 2
};

// Try to match the type of an `arch::Operand` to the type of a `xed_inst_t`.
static int MatchOperand(const Operand *instr_op,
                        const xed_operand_t *xedi_op) {
  auto op_name = xed_operand_name(xedi_op);
  auto op_width = xed_operand_width(xedi_op);
  auto instr_op_width = instr_op->BitWidth();
  switch (op_name) {
    case XED_OPERAND_IMM0SIGNED:
    case XED_OPERAND_IMM0:
    case XED_OPERAND_IMM1_BYTES:
    case XED_OPERAND_IMM1:
      if (!instr_op->IsImmediate()) return kBadSelectionScore;
      if (MatchWidth(instr_op_width, op_width)) return kGreatSelectionScore;
      return kGoodSelectionScore;

    case XED_OPERAND_RELBR:
      if (instr_op->IsBranchTarget()) {
        return kGreatSelectionScore;
      } else if (instr_op->IsImmediate()) {
        return kGoodSelectionScore;
      } else {
        return kBadSelectionScore;
      }

    case XED_OPERAND_MEM0:
    case XED_OPERAND_MEM1:
      if (!instr_op->IsMemory() || !MatchWidth(instr_op_width, op_width)) {
        return kBadSelectionScore;
      }
      return kGoodSelectionScore;

    // Note: REG2 - REG8, BASE0, and BASE1 all represent suppressed operands.
    case XED_OPERAND_REG0:
    case XED_OPERAND_REG1:
      if (!instr_op->IsRegister()) return kBadSelectionScore;
      if (!MatchWidth(instr_op_width, op_width)) return kBadSelectionScore;
      if (MatchExactReg(instr_op->reg, xedi_op)) return kGreatSelectionScore;
      return kGoodSelectionScore;

    default:
      GRANARY_ASSERT(false);
      return kBadSelectionScore;
  }
}

// Try to match the explicit operands of `instr` against the types of first
// operands of `xedi`. Returns a score for the choosing `xedi` as the selection
// for `instr`.
int MatchOperandTypes(const Instruction *instr, const xed_inst_t *xedi) {
  auto score = 0;
  for (auto i = 0U; i < instr->num_explicit_ops; ++i) {
    const auto &instr_op(instr->ops[i]);
    GRANARY_ASSERT(XED_ENCODER_OPERAND_TYPE_INVALID != instr_op.type);
    score += MatchOperand(&instr_op, xed_inst_operand(xedi, i));
  }
  return score;
}

}  // namespace

// Returns the `xed_inst_t` instance associated with this instruction. This
// won't necessarily return a perfect selection. That is, all that is required
// of the returned selection is that the types of the operands match
// (independent of the sizes of operands).
const xed_inst_t *SelectInstruction(Instruction *instr) {
  auto xedi = ICLASS_SELECTIONS[instr->iclass];
  int max_score = kBadSelectionScore;
  const xed_inst_t *max_xedi(nullptr);

  // Special case for `LEA`.
  if (GRANARY_UNLIKELY(XED_ICLASS_LEA == instr->iclass)) {
    max_xedi = xedi;
    goto select_xedi;
  }

  // Try to find the best matching instruction.
  for (; xedi < LAST_ICLASS_SELECTION; ++xedi) {
    if (xed_inst_iclass(xedi) != instr->iclass) break;
    auto score = MatchOperandTypes(instr, xedi);
    if (score >= 0 && score > max_score) {
      max_score = score;
      max_xedi = xedi;
    }
  }

select_xedi:
  GRANARY_ASSERT(nullptr != max_xedi);
  instr->iform = xed_inst_iform_enum(max_xedi);  // Update in-place.
  instr->isel = static_cast<unsigned>(max_xedi - xed_inst_table_base());
  return max_xedi;
}

}  // namespace arch
}  // namespace granary
