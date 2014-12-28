/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/cfg/operand.h"
#include "arch/driver.h"
#include "granary/breakpoint.h"

namespace granary {
namespace {
static arch::Operand * const TOMBSTONE = \
    reinterpret_cast<arch::Operand *>(0x1ULL);
}  // namespace

Operand::Operand(const Operand &that)
    : op(that.op),
      op_ptr(TOMBSTONE) {}

// Initialize an empty operand.
Operand::Operand(arch::Operand *op_)
    : op(*op_),
      op_ptr(op_) {}

// Initialize an empty operand.
Operand::Operand(const arch::Operand *op_)
    : op(*op_),
      op_ptr(TOMBSTONE) {}

// Replace the internal operand memory.
void Operand::UnsafeReplace(arch::Operand *op_) {
  op.Construct<const arch::Operand &>(*op_);
  op_ptr = op_;
}

void Operand::UnsafeReplace(const arch::Operand *op_) {
  op.Construct(*op_);
  op_ptr = TOMBSTONE;
}

// Returns a pointer to the internal, arch-specific memory operand that is
// *internal* to this `Operand`.
const arch::Operand *Operand::Extract(void) const {
  return op.AddressOf();
}

// Returns a pointer to the internal, arch-specific memory operand that is
// *referenced* by this `Operand`.
arch::Operand *Operand::UnsafeExtract(void) const {
  return op_ptr;
}

bool Operand::IsValid(void) const {
  if (TOMBSTONE == op_ptr) {
    return op->IsValid();
  } else {
    return nullptr != op_ptr;
  }
}

bool Operand::IsRead(void) const {
  return op_ptr && op->IsRead();
}

bool Operand::IsWrite(void) const {
  return op_ptr && op->IsWrite();
}

bool Operand::IsSemanticDefinition(void) const {
  return op_ptr && op->IsSemanticDefinition();
}

bool Operand::IsConditionalRead(void) const {
  return op_ptr && op->IsConditionalRead();
}

bool Operand::IsConditionalWrite(void) const {
  return op_ptr && op->IsConditionalWrite();
}

bool Operand::IsRegister(void) const {
  return op->IsRegister();
}
bool Operand::IsMemory(void) const {
  return op->IsMemory();
}

bool Operand::IsImmediate(void) const {
  return op->IsImmediate();
}

bool Operand::IsLabel(void) const {
  return op->IsBranchTarget();
}

// Convert this operand into a string.
void Operand::EncodeToString(OperandString *str) const {
  if (op_ptr) {
    op->EncodeToString(str);
  } else {
    (*str)[0] = '\0';
  }
}

}  // namespace granary
