/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/string.h"
#include "granary/cfg/operand.h"
#include "granary/arch/x86-64/instruction.h"
#include "granary/breakpoint.h"

namespace granary {
namespace {
static arch::Operand * const TOMBSTONE = \
    reinterpret_cast<arch::Operand *>(0x1ULL);
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
  op->is_compound = false;
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
  op->is_compound = false;
  op_ptr = TOMBSTONE;
}

// Returns true if this is a compound memory operation. Compound memory
// operations can have multiple smaller operands (e.g. registers) inside of
// them. An example of a compound memory operand is a `base + index * scale`
// (i.e. base/displacement) operand on x86.
bool MemoryOperand::IsCompound(void) const {
  return XED_ENCODER_OPERAND_TYPE_MEM == op->type && op->is_compound;
}

// Is this an effective address (instead of being an actual memory access).
//
// Note: This has a driver-specific implementation.
bool MemoryOperand::IsEffectiveAddress(void) const {
  return op->is_effective_address;  // Applies to PTR and MEM types.
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
  if (XED_ENCODER_OPERAND_TYPE_MEM == op->type && !op->is_compound) {
    reg = op->reg;
    return true;
  }
  return false;
}

namespace {
// Match the next register in the compound memory operand.
static void MatchNextRegister(xed_reg_enum_t reg,
                              std::initializer_list<VirtualRegister *> regs,
                              size_t *next) {
  if (XED_REG_INVALID != reg && *next < regs.size()) {
    regs.begin()[*next]->DecodeFromNative(static_cast<int>(reg));
    *next += 1;
  }
}
}  // namespace

// Try to match this memory operand as a register value. That is, the address
// is stored in the matched register.
//
// Note: This has a driver-specific implementation.
size_t MemoryOperand::CountMatchedRegisters(
    std::initializer_list<VirtualRegister *> regs) const {
  size_t num_matched(0);
  if (XED_ENCODER_OPERAND_TYPE_MEM == op->type) {
    if (op->is_compound) {
      MatchNextRegister(op->mem.reg_seg, regs, &num_matched);
      MatchNextRegister(op->mem.reg_base, regs, &num_matched);
      MatchNextRegister(op->mem.reg_index, regs, &num_matched);
    } else if (0 < regs.size()) {
      *(regs.begin()[0]) = op->reg;
      num_matched = 1;
    }
  }
  return num_matched;
}

// Initialize a new register operand from a virtual register.
RegisterOperand::RegisterOperand(const VirtualRegister reg) {
  op->type = XED_ENCODER_OPERAND_TYPE_REG;
  op->width = static_cast<int8_t>(reg.BitWidth());
  op->reg = reg;
  op->rw = XED_OPERAND_ACTION_INVALID;
  op->is_sticky = false;
  op_ptr = TOMBSTONE;
}

namespace arch {

Operand::Operand(const Operand &op) {
  memcpy(this, &op, sizeof op);
}

namespace {
// Encode a compressed memory operand into a string.
static void EncodeMemOpToString(const Operand *op, OperandString *str) {
  if (op->mem.reg_seg) {
    str->UpdateFormat("%s:", xed_reg_enum_t2str(op->mem.reg_seg));
  }
  str->UpdateFormat("[");
  if (op->mem.reg_base) {
    str->UpdateFormat("%s%s", xed_reg_enum_t2str(op->mem.reg_base),
                      op->mem.reg_index ? " + " : "");
  }
  if (op->mem.reg_index) {
    str->UpdateFormat("%s * %u", xed_reg_enum_t2str(op->mem.reg_index),
                      op->mem.scale);
  }
  if (op->mem.disp) {
    if (op->mem.disp > 0) {
      str->UpdateFormat(" + %d", op->mem.disp);
    } else {
      str->UpdateFormat(" - %d", -op->mem.disp);
    }
  }
  str->UpdateFormat("]");
}
}  // namespace

void Operand::EncodeToString(OperandString *str) const {
  auto prefix = "";
  auto suffix = "";
  switch (type) {
    case XED_ENCODER_OPERAND_TYPE_OTHER:
      str->Format("?other?");
      break;
    case XED_ENCODER_OPERAND_TYPE_INVALID:
      str->Format("?invalid?");
      break;

    case XED_ENCODER_OPERAND_TYPE_BRDISP:
      str->Format("0x%lx", addr.as_uint);
      break;

    case XED_ENCODER_OPERAND_TYPE_MEM:
      if (is_compound) {
        EncodeMemOpToString(this, str);
        break;
      } else {
        prefix = "[";
        suffix = "]";  // Fall-through.
      }

    case XED_ENCODER_OPERAND_TYPE_REG:
    case XED_ENCODER_OPERAND_TYPE_SEG0:
    case XED_ENCODER_OPERAND_TYPE_SEG1:
      if (reg.IsNative()) {
        auto arch_reg = static_cast<xed_reg_enum_t>(reg.EncodeToNative());
        str->Format("%s%s%s", prefix, xed_reg_enum_t2str(arch_reg), suffix);
      } else if (reg.IsVirtual()) {
        str->Format("%s%%%u%s", prefix, reg.Number(), suffix);
      } else {
        str->Format("%s?reg?%s", prefix, suffix);
      }
      break;

    case XED_ENCODER_OPERAND_TYPE_IMM0:
    case XED_ENCODER_OPERAND_TYPE_IMM1:
      str->Format("%lu", imm.as_uint);
      break;

    case XED_ENCODER_OPERAND_TYPE_SIMM0:
      str->Format("%ld", imm.as_int);
      break;

    case XED_ENCODER_OPERAND_TYPE_PTR:
      str->Format("[0x%lx]", addr.as_uint);
      break;
  }
}

}  // namespace arch
}  // namespace granary
