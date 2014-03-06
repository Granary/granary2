/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/string.h"
#include "granary/base/pc.h"

#include "granary/driver/xed2-intel64/decode.h"
#include "granary/driver/xed2-intel64/instruction.h"

#include "granary/breakpoint.h"

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

}  // namespace driver
}  // namespace granary
