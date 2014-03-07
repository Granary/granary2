/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/operand/operand.h"

#include "granary/driver.h"

namespace granary {

bool Operand::IsMemory(void) const {
  return op && OP_MEMORY == kind;
}

bool Operand::IsRegister(void) const {
  return op && OP_REGISTER == kind;
}

bool Operand::IsImmediate(void) const {
  return op && OP_IMMEDIATE == kind;
}

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

// Initialize an empty operand.
Operand::Operand(driver::Instruction *instr_,
                 driver::Operand *op_,
                 OperandKind kind_)
    : instr(instr_),
      op(op_),
      kind(kind_) {}

MemoryOperand::MemoryOperand(void)
    : Operand(nullptr, nullptr, OP_MEMORY) {}

MemoryOperand::MemoryOperand(driver::Instruction *instr_, driver::Operand *op_)
    : Operand(instr_, op_, OP_MEMORY) {}

RegisterOperand::RegisterOperand(void)
    : Operand(nullptr, nullptr, OP_REGISTER) {}

RegisterOperand::RegisterOperand(driver::Instruction *instr_,
                                 driver::Operand *op_)
    : Operand(instr_, op_, OP_REGISTER) {}

ImmediateOperand::ImmediateOperand(void)
    : Operand(nullptr, nullptr, OP_IMMEDIATE) {}

ImmediateOperand::ImmediateOperand(driver::Instruction *instr_,
                                   driver::Operand *op_)
    : Operand(instr_, op_, OP_IMMEDIATE) {}

}  // namespace granary
