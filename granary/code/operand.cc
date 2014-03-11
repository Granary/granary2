/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/operand.h"

#include "granary/driver.h"

namespace granary {

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (Operand, 2),
      (MemoryOperand, 2 * 3),
      (RegisterOperand, 2 * 5),
      (ImmediateOperand, 2 * 7))

GRANARY_DEFINE_BASE_CLASS(Operand)
GRANARY_DEFINE_DERIVED_CLASS_OF(Operand, MemoryOperand)
GRANARY_DEFINE_DERIVED_CLASS_OF(Operand, RegisterOperand)
GRANARY_DEFINE_DERIVED_CLASS_OF(Operand, ImmediateOperand)

bool Operand::IsRead(void) const {
  return op && op->IsRead();
}

bool Operand::IsWrite(void) const {
  return op && op->IsWrite();
}

bool Operand::IsConditionalRead(void) const {
  return op && op->IsConditionalRead();
}

bool Operand::IsConditionalWrite(void) const {
  return op && op->IsConditionalWrite();
}

// Convert this operand into a string.
void Operand::EncodeToString(OperandString *str) const {
  if (op) {
    op->EncodeToString(str);  // TODO(pag): Implement me.
  } else {
    (*str)[0] = '\0';
  }
}

// Initialize an empty operand.
Operand::Operand(driver::Instruction *instr_,
                 driver::Operand *op_)
    : instr(instr_),
      op(op_) {}

}  // namespace granary
