/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/driver/xed2-intel64/builder.h"
#include "granary/driver/xed2-intel64/instruction.h"

namespace granary {
namespace driver {

namespace {
// Returns the bit width of an immediate integer. This is to calculate operand
// width when using the instruction builder IR.
static int ImmediateWidthBits(uint64_t imm) {
  enum : uint64_t {
    WIDTH_8   = 0x0FFUL,
    WIDTH_16  = WIDTH_8 | (WIDTH_8 << 8),
    WIDTH_32  = WIDTH_16 | (WIDTH_16 << 16)
  };
  if (!imm) return 1;
  if ((imm | ~WIDTH_8) == imm) return 8;  // Signed.
  if ((imm & WIDTH_8) == imm) return 8;  // Unsigned.

  if ((imm | ~WIDTH_16) == imm) return 16;  // Signed.
  if ((imm & WIDTH_16) == imm) return 16;  // Unsigned.

  if ((imm | ~WIDTH_32) == imm) return 32;  // Signed.
  if ((imm & WIDTH_32) == imm) return 32;  // Unsigned.
  return 64;
}
}  // namespace

// Initialize an emptry Granary `driver::Instruction` from a XED iclass,
// category, and the number of explicit operands.
void BuildInstruction(Instruction *instr, xed_iclass_enum_t iclass,
                      xed_category_enum_t category, uint8_t num_explicit_ops) {
  memset(instr, 0, sizeof *instr);
  instr->iclass = iclass;
  instr->category = category;
  instr->num_explicit_ops = num_explicit_ops;

  // Make all implicit / suppressed operands sticky.
  for (uint8_t i = num_explicit_ops; i < Instruction::MAX_NUM_OPS; ++i) {
    instr->ops[i].is_sticky = true;
  }
}

// Add this register as an operand to the instruction `instr`.
void RegisterBuilder::Build(Instruction *instr) {
  auto &op(instr->ops[instr->num_ops++]);
  op.type = XED_ENCODER_OPERAND_TYPE_REG;
  op.reg = reg;
  op.rw = action;
  op.width = static_cast<int8_t>(reg.BitWidth());
}

// Add this immediate as an operand to the instruction `instr`.
void ImmediateBuilder::Build(Instruction *instr) {
  auto &op(instr->ops[instr->num_ops++]);
  op.imm.as_uint = as_uint;
  op.type = type;
  op.rw = XED_OPERAND_ACTION_R;
  op.width = static_cast<int8_t>(ImmediateWidthBits(as_uint));
}

// Add this memory as an operand to the instruction `instr`.
void MemoryBuilder::Build(Instruction *instr) {
  auto &op(instr->ops[instr->num_ops++]);
  if (is_ptr) {
    op.type = XED_ENCODER_OPERAND_TYPE_PTR;
    op.addr.as_ptr = ptr;
  } else {
    op.type = XED_ENCODER_OPERAND_TYPE_MEM;
    op.reg = reg;
  }
  op.rw = action;
  op.width = -1;  // Unknown.
}

// Add this branch target as an operand to the instruction `instr`.
void BranchTargetBuilder::Build(Instruction *instr) {
  auto &op(instr->ops[instr->num_ops++]);
  op.branch_target.as_pc = pc;
  op.rw = XED_OPERAND_ACTION_R;
  op.type = XED_ENCODER_OPERAND_TYPE_BRDISP;
  op.width = arch::ADDRESS_WIDTH_BITS;
}

}  // namespace driver
}  // namespace granary
