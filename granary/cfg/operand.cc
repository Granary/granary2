/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/cfg/operand.h"
#include "granary/driver/driver.h"
#include "granary/breakpoint.h"

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

namespace {
static driver::Operand * const TOMBSTONE = \
    reinterpret_cast<driver::Operand *>(0x1ULL);
}  // namespace

// Returns true if this `OperandRef` references a memory operand, and if so,
// updates `mem_op` to have the value of the referenced operand.
//
// Note: This operation is only valid if `OperandRef::IsValid` returns true.
bool OperandRef::Match(MemoryOperand &mem_op) const {
  GRANARY_ASSERT(IsValid());
  if (op->IsMemory()) {
    mem_op.UnsafeReplace(op);
    return true;
  }
  return false;
}

// Returns true if this `OperandRef` references a register operand, and if so,
// updates `reg_op` to have the value of the referenced operand.
//
// Note: This operation is only valid if `OperandRef::IsValid` returns true.
bool OperandRef::Match(RegisterOperand &reg_op) const {
  GRANARY_ASSERT(IsValid());
  if (op->IsRegister()) {
    reg_op.UnsafeReplace(op);
    return true;
  }
  return false;
}

// Returns true if this `OperandRef` references an immediate operand, and if
// so, updates `imm_op` to have the value of the referenced operand.
//
// Note: This operation is only valid if `OperandRef::IsValid` returns true.
bool OperandRef::Match(ImmediateOperand &imm_op) const {
  GRANARY_ASSERT(IsValid());
  if (op->IsImmediate()) {
    imm_op.UnsafeReplace(op);
    return true;
  }
  return false;
}

// Returns true if this is a valid reference.
bool OperandRef::IsValid(void) const {
  return op && TOMBSTONE != op;
}

Operand::Operand(const Operand &that)
    : op(that.op),
      op_ptr(TOMBSTONE) {}

// Initialize an empty operand.
Operand::Operand(driver::Operand *op_)
    : op(*op_),
      op_ptr(op_) {}

// Replace the internal operand memory.
void Operand::UnsafeReplace(driver::Operand *op_) {
  op.Construct<driver::Operand &>(*op_);
  op_ptr = op_;
}

bool Operand::IsRead(void) const {
  return op_ptr && op->IsRead();
}

bool Operand::IsWrite(void) const {
  return op_ptr && op->IsWrite();
}

bool Operand::IsConditionalRead(void) const {
  return op_ptr && op->IsConditionalRead();
}

bool Operand::IsConditionalWrite(void) const {
  return op_ptr && op->IsConditionalWrite();
}

// Returns whether or not this operand can be replaced / modified.
//
// Note: This has a driver-specific implementation.
bool Operand::IsModifiable(void) const {
  return !op->is_sticky;
}

// Returns whether or not this operand is explicit.
bool Operand::IsExplicit(void) const {
  GRANARY_ASSERT(op_ptr && TOMBSTONE != op_ptr);
  return op_ptr->is_sticky;
}

// Return the width (in bits) of this operand, or -1 if its width is not
// known.
int Operand::Width(void) const {
  return op->width;
}

// Convert this operand into a string.
void Operand::EncodeToString(OperandString *str) const {
  if (op_ptr) {
    op->EncodeToString(str);  // TODO(pag): Implement me.
  } else {
    (*str)[0] = '\0';
  }
}

// Convert this operand into a reference, so that we can then replace it the
// backing operand.
OperandRef Operand::Ref(void) const {
  GRANARY_ASSERT(op_ptr && TOMBSTONE != op_ptr);
  return OperandRef(op_ptr);
}

}  // namespace granary