/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/operand.h"

#include "granary/driver/decode.h"
#include "granary/driver/xed2-intel64/instruction.h"

namespace granary {
namespace driver {

Instruction::Instruction(void) {
  memset(this, 0, sizeof *this);
  iclass = XED_ICLASS_INVALID;
  category = XED_CATEGORY_INVALID;
}

Instruction::Instruction(const Instruction &that) {
  memcpy(this, &that, sizeof that);
}

bool Instruction::HasIndirectTarget(void) const {
  if (IsFunctionCall() || IsUnconditionalJump()) {
    return XED_ENCODER_OPERAND_TYPE_REG == ops[0].type ||
           XED_ENCODER_OPERAND_TYPE_MEM == ops[0].type;
  }
  return IsFunctionReturn() || IsInterruptCall() || IsInterruptReturn() ||
         IsSystemCall() || IsSystemReturn();
}

// Get the opcode name.
const char *Instruction::OpCodeName(void) const {
  return xed_iclass_enum_t2str(iclass);
}

// Invoke a function on every operand.
void Instruction::ForEachOperand(std::function<void(granary::Operand *)> func) {
  for (auto &op : ops) {
    if (XED_ENCODER_OPERAND_TYPE_INVALID == op.type) {
      break;
    }
    switch (op.type) {
      case XED_ENCODER_OPERAND_TYPE_REG:
      case XED_ENCODER_OPERAND_TYPE_SEG0:
      case XED_ENCODER_OPERAND_TYPE_SEG1: {
        RegisterOperand reg(this, &op);
        func(reinterpret_cast<granary::Operand *>(&reg));
        break;
      }

      case XED_ENCODER_OPERAND_TYPE_BRDISP:
      case XED_ENCODER_OPERAND_TYPE_IMM0:
      case XED_ENCODER_OPERAND_TYPE_SIMM0:
      case XED_ENCODER_OPERAND_TYPE_IMM1: {
        ImmediateOperand imm(this, &op);
        func(reinterpret_cast<granary::Operand *>(&imm));
        break;
      }
      case XED_ENCODER_OPERAND_TYPE_MEM:
      case XED_ENCODER_OPERAND_TYPE_PTR: {
        MemoryOperand mem(this, &op);
        func(reinterpret_cast<granary::Operand *>(&mem));
        break;
      }
      default: break;  // TODO(pag): Implement others.
    }
  }
}

}  // namespace driver
}  // namespace granary
