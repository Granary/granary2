/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/operand/operand.h"

#include "granary/driver.h"

namespace granary {

bool Operand::IsRead(void) const {
  return driver_op && driver_op->IsRead();
}

bool Operand::IsWrite(void) const {
  return driver_op && driver_op->IsWrite();
}

bool Operand::IsConditionalRead(void) const {
  return driver_op && driver_op->IsConditionalRead();
}

bool Operand::IsConditionalWrite(void) const {
  return driver_op && driver_op->IsConditionalWrite();
}

// Initialize an empty operand.
Operand::Operand(void)
    : native_instr(nullptr),
      driver_instr(nullptr),
      driver_op(nullptr),
      kind(OP_UNDEFINED) {}

}  // namespace granary
