/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/string.h"
#include "granary/cfg/operand.h"
#include "granary/driver/xed2-intel64/instruction.h"
#include "granary/breakpoint.h"

namespace granary {
namespace {
static driver::Operand * const TOMBSTONE = \
    reinterpret_cast<driver::Operand *>(0x1ULL);
}  // namespace

// Try to replace the referenced operand with a concrete operand. Returns
// false if the referenced operand is not allowed to be replaced. For example,
// suppressed and implicit operands cannot be replaced.
bool OperandRef::ReplaceWith(const Operand &repl_op) {
  GRANARY_ASSERT(op && TOMBSTONE != op && repl_op.op_ptr);
  if (GRANARY_UNLIKELY(op->is_sticky)) {
    return false;
  } else {
    auto rw = op->rw;
    auto width = op->width;
    *op = *(repl_op.op.AddressOf());
    op->rw = rw;
    op->width = width;
    return true;
  }
}

bool RegisterOperand::IsNative(void) const {
  return op->reg.IsNative();
}

bool RegisterOperand::IsVirtual(void) const {
  return op->reg.IsVirtual();
}

// Extract the register.
VirtualRegister RegisterOperand::Register(void) const {
  return op->reg;
}

// Initialize a new memory operand from a virtual register, where the
// referenced memory has a width of `num_bits`.
MemoryOperand::MemoryOperand(const VirtualRegister &ptr_reg, int num_bits) {
  op->type = XED_ENCODER_OPERAND_TYPE_MEM;
  op->width = static_cast<int8_t>(num_bits);
  op->reg = ptr_reg;
  op->rw = XED_OPERAND_ACTION_INVALID;
  op->is_sticky = false;
  op_ptr = TOMBSTONE;
}

// Initialize a new memory operand from a pointer, where the
// referenced memory has a width of `num_bits`.
MemoryOperand::MemoryOperand(const void *ptr, int num_bits) {
  op->type = XED_ENCODER_OPERAND_TYPE_PTR;
  op->width = static_cast<int8_t>(num_bits);
  op->addr.as_ptr = ptr;
  op->rw = XED_OPERAND_ACTION_INVALID;
  op->is_sticky = false;
  op_ptr = TOMBSTONE;
}

// Try to match this memory operand as a pointer value.
bool MemoryOperand::MatchPointer(const void *&ptr) const {
  if (XED_ENCODER_OPERAND_TYPE_PTR == op->type) {
    ptr = op->addr.as_ptr;
    return true;
  }
  return false;
}

// Try to match this memory operand as a register value. That is, the address
// is stored in the matched register.
bool MemoryOperand::MatchRegister(VirtualRegister &reg) const {
  if (XED_ENCODER_OPERAND_TYPE_MEM == op->type) {
    reg = op->reg;
    return true;
  }
  return false;
}

// Initialize a new register operand from a virtual register.
RegisterOperand::RegisterOperand(const VirtualRegister &reg) {
  op->type = XED_ENCODER_OPERAND_TYPE_REG;
  op->width = static_cast<int8_t>(reg.BitWidth());
  op->reg = reg;
  op->rw = XED_OPERAND_ACTION_INVALID;
  op->is_sticky = false;
  op_ptr = TOMBSTONE;
}

namespace driver {

Operand::Operand(const Operand &op) {
  memcpy(this, &op, sizeof op);
}

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
