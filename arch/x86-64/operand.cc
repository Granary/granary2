/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/operand.h"

#include "arch/x86-64/instruction.h"
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
  if (GRANARY_UNLIKELY(op->is_sticky || !op->is_explicit)) {
    return false;
  } else {
    *op = *(repl_op.op.AddressOf());
    return true;
  }
}

// Returns whether or not this operand can be replaced / modified.
bool Operand::IsModifiable(void) const {

  static_assert(sizeof(op) >= sizeof(arch::Operand) &&
                alignof(op) >= alignof(arch::Operand) &&
                0 == (alignof(op) % alignof(arch::Operand)),
                "Invalid size or alignment for `OpaqueContainer`.");

  return op->is_explicit && !op->is_sticky;
}

// Returns whether or not this operand is explicit.
//
// Note: This is only valid on operands matched from instructions and not on
//       manually created operands.
bool Operand::IsExplicit(void) const {
  GRANARY_ASSERT(op_ptr && TOMBSTONE != op_ptr);
  return op_ptr->is_explicit;
}

// Return the width (in bits) of this operand, or -1 if its width is not
// known.
int Operand::BitWidth(void) const {
  return op->width;
}

// Return the width (in bytes) of this operand, or -1 if its width is not
// known.
int Operand::ByteWidth(void) const {
  return -1 != op->width ? op->width / 8 : op->width;
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
// referenced memory has a width of `num_bytes`.
MemoryOperand::MemoryOperand(const VirtualRegister &ptr_reg, int num_bytes) {
  op->type = XED_ENCODER_OPERAND_TYPE_MEM;
  op->width = static_cast<int16_t>(0 < num_bytes ? num_bytes * 8 : -1);
  op->reg = ptr_reg;
  op->rw = XED_OPERAND_ACTION_INVALID;
  op->is_sticky = false;
  op->is_compound = false;
  op_ptr = TOMBSTONE;
}

// Initialize a new memory operand from a pointer, where the
// referenced memory has a width of `num_bytes`.
MemoryOperand::MemoryOperand(const void *ptr, int num_bytes) {
  op->type = XED_ENCODER_OPERAND_TYPE_PTR;
  op->width = static_cast<int16_t>(0 < num_bytes ? num_bytes * 8 : -1);
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
bool MemoryOperand::IsEffectiveAddress(void) const {
  return op->is_effective_address;  // Applies to PTR and MEM types.
}

// Try to match this memory operand as a pointer value.
bool MemoryOperand::IsPointer(void) const {
  return XED_ENCODER_OPERAND_TYPE_PTR == op->type;
}

// Try to match this memory operand as a pointer value.
bool MemoryOperand::MatchPointer(const void *&ptr) const {
  if (XED_ENCODER_OPERAND_TYPE_PTR == op->type) {
    if (XED_REG_INVALID == op->segment || XED_REG_DS == op->segment) {
      ptr = op->addr.as_ptr;
      return true;
    }
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
size_t MemoryOperand::CountMatchedRegisters(
    std::initializer_list<VirtualRegister *> regs) const {
  size_t num_matched(0);
  if (XED_ENCODER_OPERAND_TYPE_MEM == op->type) {
    if (op->is_compound) {
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
RegisterOperand::RegisterOperand(const VirtualRegister reg)
    : Operand() {
  op->type = XED_ENCODER_OPERAND_TYPE_REG;
  op->width = static_cast<int16_t>(reg.BitWidth());
  op->reg = reg;
  op->rw = XED_OPERAND_ACTION_INVALID;
  op->is_sticky = false;
  op_ptr = TOMBSTONE;
}

// Initialize a immediate operand from a signed integer, where the value has
// a width of `width_bytes`.
ImmediateOperand::ImmediateOperand(intptr_t imm, int width_bytes)
    : Operand() {
  op->type = XED_ENCODER_OPERAND_TYPE_SIMM0;
  op->width = static_cast<int16_t>(width_bytes * 8);
  op->imm.as_int = imm;
  op->rw = XED_OPERAND_ACTION_R;
  op->is_sticky = false;
  op_ptr = TOMBSTONE;
}

// Initialize a immediate operand from a unsigned integer, where the value
// has a width of `width_bytes`.
ImmediateOperand::ImmediateOperand(uintptr_t imm, int width_bytes)
    : Operand() {
  op->type = XED_ENCODER_OPERAND_TYPE_IMM0;
  op->width = static_cast<int16_t>(width_bytes * 8);
  op->imm.as_uint = imm;
  op->rw = XED_OPERAND_ACTION_R;
  op->is_sticky = false;
  op_ptr = TOMBSTONE;
}

// Extract the value as an unsigned integer.
uint64_t ImmediateOperand::UInt(void) {
  return op->imm.as_uint;
}

// Extract the value as a signed integer.
int64_t ImmediateOperand::Int(void) {
  return op->imm.as_int;
}

// Initialize a label operand from a non-null pointer to a label.
LabelOperand::LabelOperand(LabelInstruction *label)
    : Operand() {
  op->type = XED_ENCODER_OPERAND_TYPE_BRDISP;
  op->width = -1;
  op->annotation_instr = label;
  op->is_annotation_instr = true;
  op->rw = XED_OPERAND_ACTION_R;
  op->is_sticky = false;
  op_ptr = TOMBSTONE;
}

// Target of a label operand.
//
// Note: This has a driver-specific implementation.
AnnotationInstruction *LabelOperand::Target(void) const {
  return op->annotation_instr;
}

namespace arch {

Operand::Operand(const Operand &that) {
  memcpy(this, &that, sizeof that);
}

Operand &Operand::operator=(const Operand &that) {
  if (&that != this) {
    const auto old_rw = rw;
    const auto old_width = width;
    const auto old_is_ea = is_effective_address;
    const auto old_segment = segment;
    memcpy(this, &that, sizeof that);
    if (-1 != old_width) width = old_width;
    rw = old_rw;
    is_effective_address = old_is_ea;
    is_explicit = true;
    is_sticky = false;
    if (XED_REG_INVALID != old_segment && XED_REG_DS != old_segment) {
      segment = old_segment;
    }
  }
  return *this;
}

namespace {
// Encode a compressed memory operand into a string.
static void EncodeMemOpToString(const Operand *op, OperandString *str) {
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
      GRANARY_ASSERT(op->mem.reg_base || op->mem.reg_index);
      str->UpdateFormat(" + 0x%x", op->mem.disp);
    } else {
      str->UpdateFormat(" - 0x%x", -op->mem.disp);
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
      if (is_annotation_instr) {
        str->Format("LABEL %lx", reinterpret_cast<uintptr_t>(annotation_instr));
      } else {
        str->Format("0x%lx", addr.as_uint);
      }
      break;

    case XED_ENCODER_OPERAND_TYPE_MEM:
      str->UpdateFormat("m%d ", static_cast<int>(width));
      if (XED_REG_INVALID != segment) {
        str->UpdateFormat("%s:", xed_reg_enum_t2str(segment));
      }
      if (is_compound) {
        EncodeMemOpToString(this, str);
        break;
      } else {
        prefix = "[";
        suffix = "]";
      }
    [[clang::fallthrough]];
    case XED_ENCODER_OPERAND_TYPE_REG:
    case XED_ENCODER_OPERAND_TYPE_SEG0:
    case XED_ENCODER_OPERAND_TYPE_SEG1:
      if (reg.IsNative()) {
        auto arch_reg = static_cast<xed_reg_enum_t>(reg.EncodeToNative());
        str->UpdateFormat("%sr%d %s%s", prefix, reg.BitWidth(),
                          xed_reg_enum_t2str(arch_reg), suffix);
      } else if (reg.IsVirtual()) {
        str->UpdateFormat("%s%%%u%s", prefix, reg.Number(), suffix);
      } else if (reg.IsVirtualSlot()) {
        str->UpdateFormat("%sSLOT:%d%s", prefix, reg.Number(), suffix);
      } else {
        str->UpdateFormat("%s?reg?%s", prefix, suffix);
      }
      break;

    case XED_ENCODER_OPERAND_TYPE_IMM0:
    case XED_ENCODER_OPERAND_TYPE_IMM1:
    case XED_ENCODER_OPERAND_TYPE_SIMM0:
      if (imm.as_int >= 0) {
        str->UpdateFormat("0x%lx", imm.as_uint);
      } else {
        str->UpdateFormat("-0x%lx", -imm.as_int);
      }
      break;

    case XED_ENCODER_OPERAND_TYPE_PTR:
      str->UpdateFormat("m%d ", static_cast<int>(width));
      if (XED_REG_INVALID != segment) {
        str->UpdateFormat("%s:", xed_reg_enum_t2str(segment));
      }
      if (is_annotation_instr) {
        str->UpdateFormat("[return address]");
      } else {
        if (addr.as_int >= 0) {
          str->UpdateFormat("[0x%lx]", addr.as_uint);
        } else {
          str->UpdateFormat("[-0x%lx]", -addr.as_int);
        }
      }
      break;
  }
}

#include "generated/xed2-intel64/ambiguous_operands.cc"

}  // namespace arch
}  // namespace granary
