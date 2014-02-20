/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/instruction.h"

#define GRANARY_DEFINE_XED_REG(mnemonic) \
    Register GRANARY_CAT(REG_, mnemonic)(GRANARY_CAT(XED_REG_, mnemonic));

#include "granary/driver/xed2-intel64/builder.h"

namespace granary {
namespace driver {

// Returns the bit width of an immediate integer. This is to calculate operand
// width when using the instruction builder IR.
unsigned ImmediateWidth(uint64_t imm) {
  enum {
    WIDTH_8   = 0x0FFUL,
    WIDTH_16  = WIDTH_8 | (WIDTH_8 << 8),
    WIDTH_32  = WIDTH_16 | (WIDTH_16 << 16)
  };
  if (!imm) return 0;
  if ((imm & WIDTH_8) == imm) return 8;
  if ((imm & WIDTH_16) == imm) return 16;
  if ((imm & WIDTH_32) == imm) return 32;
  return 64;
}

// Import XED instruction information into Granary's low-level IR. This
// initializes a number of the internal `Instruction` fields to sane defaults.
void ImportInstruction(Instruction *instr, xed_iclass_enum_t iclass,
                       xed_category_enum_t category, int8_t num_ops) {
  instr->iclass = iclass;
  instr->category = category;
  instr->prefixes.i = 0;
  instr->length = 0;
  instr->num_ops = num_ops;
  instr->needs_encoding = true;
  instr->has_pc_rel_op = false;
  instr->has_fixed_length = false;
  instr->is_atomic = false;
  instr->has_memory_op = false;
}

// Import a register operand into Granary's low-level IR.
void ImportOperand(Instruction *, Operand *op, Register reg) {
  op->type = XED_ENCODER_OPERAND_TYPE_REG;
  op->u.reg = reg.reg;
  op->width = xed_get_register_width_bits64(reg.reg);
}

// Import a base/displacement memory operand into Granary's low-level IR.
void ImportOperand(Instruction *instr, Operand *op, BaseDisp bdisp) {
  granary_break_on_fault_if(XED_REG_RIP == bdisp.base);
  op->type = XED_ENCODER_OPERAND_TYPE_MEM;
  op->u.mem = bdisp;
  op->width = 0;  // Inherit size.
  instr->has_memory_op = true;
}

// Import a relative address operand into Granary's low-level IR.
void ImportOperand(Instruction *instr, Operand *op, RelativeAddress addr) {
  if (instr->IsJump() || instr->IsFunctionCall()) {
    op->type = XED_ENCODER_OPERAND_TYPE_BRDISP;
  } else {
    op->type = XED_ENCODER_OPERAND_TYPE_PTR;
    if (XED_ICLASS_LEA != instr->iclass) {
      instr->has_memory_op = true;
    }
  }
  op->rel.imm = addr.addr;
  op->width = 64;
  instr->has_pc_rel_op = true;
}

// Import an immediate operand into Granary's low-level IR.
void ImportOperand(Instruction *, Operand *op, Immediate imm,
                   xed_encoder_operand_type_t type) {
  op->type = type;
  if (XED_ENCODER_OPERAND_TYPE_IMM1 == type) {
    op->u.imm1 = static_cast<decltype(op->u.imm1)>(imm.value);
  } else {
    op->u.imm0 = imm.value;
  }
  op->width = imm.width;
}

}  // namespace driver
}  // namespace granary
