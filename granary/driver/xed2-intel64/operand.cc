/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/string.h"

#include "granary/driver/xed2-intel64/instruction.h"

#include "granary/code/operand.h"

namespace granary {

bool RegisterOperand::IsNative(void) const {
  return op && op->reg.IsNative();
}

bool RegisterOperand::IsVirtual(void) const {
  return op && op->reg.IsNative();
}

namespace driver {

void Operand::EncodeToString(OperandString *str) const {
  auto prefix = "";
  auto suffix = "";
  switch (type) {
    case XED_ENCODER_OPERAND_TYPE_OTHER:
    case XED_ENCODER_OPERAND_TYPE_INVALID:
      Format(str->Buffer(), str->MaxLength(), "?");
      break;

    case XED_ENCODER_OPERAND_TYPE_BRDISP:
      Format(str->Buffer(), str->MaxLength(), "0x%lx", addr.as_uint);
      break;

    case XED_ENCODER_OPERAND_TYPE_MEM:
      prefix = "[";
      suffix = "]";
      // Fall-through.

    case XED_ENCODER_OPERAND_TYPE_REG:
    case XED_ENCODER_OPERAND_TYPE_SEG0:
    case XED_ENCODER_OPERAND_TYPE_SEG1:
      Format(str->Buffer(), str->MaxLength(), "%%");
      if (reg.IsNative()) {
        auto arch_reg = static_cast<xed_reg_enum_t>(reg.EncodeToNative());
        Format(str->Buffer(), str->MaxLength(), "%s%s%s",
               prefix, xed_reg_enum_t2str(arch_reg), suffix);
      } else if (reg.IsVirtual()) {
        Format(str->Buffer(), str->MaxLength(), "%s%%%u%s",
               prefix, reg.Number(), suffix);
      } else {
        Format(str->Buffer(), str->MaxLength(), "%s%%?%s", prefix, suffix);
      }
      break;

    case XED_ENCODER_OPERAND_TYPE_IMM0:
    case XED_ENCODER_OPERAND_TYPE_IMM1:
      Format(str->Buffer(), str->MaxLength(), "%lu", imm.as_uint);
      break;

    case XED_ENCODER_OPERAND_TYPE_SIMM0:
      Format(str->Buffer(), str->MaxLength(), "%ld", imm.as_int);
      break;

    case XED_ENCODER_OPERAND_TYPE_PTR:
      Format(str->Buffer(), str->MaxLength(), "[0x%lx]", addr.as_uint);
      break;
  }
}

}  // namespace driver
}  // namespace granary
