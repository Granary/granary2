/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/instruction.h"

#define GRANARY_DEFINE_XED_REG(mnemonic) \
    Register GRANARY_CAT(REG_, mnemonic)(GRANARY_CAT(XED_REG_, mnemonic));

#include "granary/driver/xed2-intel64/builder.h"

namespace granary {
namespace driver {

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
}

void ImportOperand(Instruction *, Operand *op, Register reg) {
  op->type = XED_ENCODER_OPERAND_TYPE_REG;
  op->u.reg = reg.reg;
  op->width = xed_get_register_width_bits64(reg.reg);
}

void ImportOperand(Instruction *instr, Operand *op, BaseDisp bdisp) {
  op->type = XED_ENCODER_OPERAND_TYPE_MEM;
  op->u.mem = bdisp;
  op->width = 0;  // Inherit size.
  if (XED_REG_RIP == bdisp.base) {
    instr->has_pc_rel_op = true;
  }
}

void ImportOperand(Instruction *instr, Operand *op, RelativeAddress addr) {
  if (XED_ICLASS_LEA == instr->iclass) {
    op->type = XED_ENCODER_OPERAND_TYPE_IMM0;
  } else {
    op->type = XED_ENCODER_OPERAND_TYPE_BRDISP;
  }
  op->rel.imm = addr.addr;
  op->width = 64;
  instr->has_pc_rel_op = true;
}

//
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
