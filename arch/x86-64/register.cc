/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/base.h"

#include "arch/x86-64/xed.h"
#include "arch/x86-64/register.h"

#include "granary/cfg/instruction.h"

#include "granary/code/register.h"

#include "granary/breakpoint.h"

namespace granary {

// See `http://sandpile.org/x86/gpr.htm` for more details on general-purpose
// registers in x86-64.
enum {
  LOW_BYTE          = 0x1,
  BYTE_2            = 0x2,
  LOW_2_BYTES       = 0x3,
  LOW_4_BYTES       = 0xF,
  ALL_8_BYTES       = 0xFF,

  HIGH_6_BYTES      = 0xFC,
  HIGH_7_BYTES      = 0xFE,
  HIGH_6_LOW_1_BYTE = 0xFD
};

// Convert an architectural register into a virtual register.
void VirtualRegister::DecodeFromNative(uint32_t reg_) {
  value = 0;  // Reset.

  const auto reg = static_cast<xed_reg_enum_t>(reg_);
  if (XED_REG_INVALID == reg || XED_REG_LAST <= reg_) {
    kind = kVirtualRegisterKindInvalid;
    return;
  }

  // Treat all arch regs as scheduled.
  is_scheduled = true;

  const auto widest_reg = xed_get_largest_enclosing_register(reg);
  num_bytes = static_cast<uint8_t>(xed_get_register_width_bits64(reg) / 8);
  is_stack_pointer = XED_REG_RSP == widest_reg;

  // Non-general-purpose registers are treated as "fixed" architectural
  // registers.
  if (XED_REG_RAX > widest_reg || XED_REG_R15 < widest_reg ||
      is_stack_pointer) {
    kind = kVirtualRegisterKindUnschedulable;
    reg_num = static_cast<decltype(reg_num)>(reg_);
    return;
  }

  // General-purpose registers are disambiguated in terms of their "widest"
  // enclosing register, and then specialized in terms of their width and which
  // bytes are actually named by the register.
  kind = kVirtualRegisterKindArchGpr;
  reg_num = widest_reg - XED_REG_RAX;
  if (XED_REG_RSP < widest_reg) {
    reg_num -= 1;  // Directly map registers to indexes.
  }

  // Mark this as potentially being a legacy register. This affects register
  // scheduling.
  is_legacy = XED_REG_AH <= reg && reg <= XED_REG_BH;

  switch (reg) {
    case XED_REG_AX: case XED_REG_CX: case XED_REG_DX: case XED_REG_BX:
    case XED_REG_BP: case XED_REG_SI: case XED_REG_DI: case XED_REG_R8W:
    case XED_REG_R9W: case XED_REG_R10W: case XED_REG_R11W: case XED_REG_R12W:
    case XED_REG_R13W: case XED_REG_R14W: case XED_REG_R15W:
      byte_mask = LOW_2_BYTES;
      preserved_byte_mask = HIGH_6_BYTES;
      return;
    case XED_REG_EAX: case XED_REG_ECX: case XED_REG_EDX: case XED_REG_EBX:
    case XED_REG_EBP: case XED_REG_ESI: case XED_REG_EDI: case XED_REG_R8D:
    case XED_REG_R9D: case XED_REG_R10D: case XED_REG_R11D: case XED_REG_R12D:
    case XED_REG_R13D: case XED_REG_R14D: case XED_REG_R15D:
      byte_mask = LOW_4_BYTES;  // 4 high-order bytes are zero extended.
      return;
    case XED_REG_RAX: case XED_REG_RCX: case XED_REG_RDX: case XED_REG_RBX:
    case XED_REG_RBP: case XED_REG_RSI: case XED_REG_RDI: case XED_REG_R8:
    case XED_REG_R9: case XED_REG_R10: case XED_REG_R11: case XED_REG_R12:
    case XED_REG_R13: case XED_REG_R14: case XED_REG_R15:
      byte_mask = ALL_8_BYTES;
      return;
    case XED_REG_AL: case XED_REG_CL: case XED_REG_DL: case XED_REG_BL:
    case XED_REG_BPL: case XED_REG_SIL: case XED_REG_DIL: case XED_REG_R8B:
    case XED_REG_R9B: case XED_REG_R10B: case XED_REG_R11B: case XED_REG_R12B:
    case XED_REG_R13B: case XED_REG_R14B: case XED_REG_R15B:
      byte_mask = LOW_BYTE;
      preserved_byte_mask = HIGH_7_BYTES;
      return;
    case XED_REG_AH: case XED_REG_CH: case XED_REG_DH: case XED_REG_BH:
      byte_mask = BYTE_2;
      preserved_byte_mask = HIGH_6_LOW_1_BYTE;
      return;
    default: GRANARY_ASSERT(false);
  }
}

// Convert a virtual register into its associated architectural register.
uint32_t VirtualRegister::EncodeToNative(void) const {
  if (kVirtualRegisterKindUnschedulable == kind) {
    return static_cast<uint32_t>(reg_num);
  } else if (kVirtualRegisterKindArchGpr != kind) {
    return XED_REG_INVALID;
  }

  // Map register numbers to XED registers.
  auto widest_reg = static_cast<xed_reg_enum_t>(reg_num + XED_REG_RAX);
  if (XED_REG_RSP <= widest_reg) {
    widest_reg = static_cast<xed_reg_enum_t>(
        static_cast<uint32_t>(widest_reg) + 1);
  }

  GRANARY_ASSERT(XED_REG_RSP != widest_reg);

  switch (byte_mask) {
    case LOW_2_BYTES: return widest_reg - (XED_REG_RAX - XED_REG_AX);
    case LOW_4_BYTES: return widest_reg - (XED_REG_RAX - XED_REG_EAX);
    case ALL_8_BYTES: return widest_reg;
    case LOW_BYTE: return widest_reg + (XED_REG_AL - XED_REG_RAX);
    case BYTE_2: return widest_reg + (XED_REG_AH - XED_REG_RAX);
    default: return XED_REG_INVALID;
  }
}

// Return the flags register as a virtual register.
//
// Note: This has an architecture-specific implementation.
VirtualRegister VirtualRegister::Flags(void) {
  return arch::REG_RFLAGS;
}

// Return the instruction pointer register as a virtual register.
//
// Note: This has an architecture-specific implementation.
VirtualRegister VirtualRegister::InstructionPointer(void) {
  return arch::REG_RIP;
}

// Return the stack pointer register as a virtual register.
//
// Note: This has an architecture-specific implementation.
VirtualRegister VirtualRegister::StackPointer(void) {
  return arch::REG_RSP;
}

// Return the frame pointer register as a virtual register.
//
// Note: This has an architecture-specific implementation.
VirtualRegister VirtualRegister::FramePointer(void) {
  // TODO(pag): This has an ABI-specific implementation.
  return arch::REG_RBP;
}

// Returns the effective size (in bytes) of a write to this register. This
// could be bigger than the width of the register in bytes.
//
// Note: This has an architecture-specific implementation.
size_t VirtualRegister::EffectiveWriteWidth(void) const {
  switch (preserved_byte_mask) {
    case 0: return arch::GPR_WIDTH_BYTES;
    case LOW_BYTE: return 1;
    case HIGH_6_BYTES: return 2;
    case HIGH_7_BYTES: return 1;
    case HIGH_6_LOW_1_BYTE: return 1;
    default:
      GRANARY_ASSERT(false);
      return arch::GPR_WIDTH_BYTES;
  }
}

// Widen this virtual register to a specific bit width.
void VirtualRegister::Widen(size_t dest_byte_width) {
  switch (dest_byte_width) {
    case 1:
      num_bytes = 1;
      byte_mask = LOW_BYTE;
      preserved_byte_mask = HIGH_7_BYTES;
      return;
    case 2:
      num_bytes = 2;
      byte_mask = LOW_2_BYTES;
      preserved_byte_mask = HIGH_6_BYTES;
      return;
    case 4:
      num_bytes = 4;
      byte_mask = LOW_4_BYTES;
      preserved_byte_mask = 0;
      return;
    case 8:
      num_bytes = 8;
      byte_mask = ALL_8_BYTES;
      preserved_byte_mask = 0;
      return;
    default: GRANARY_ASSERT(false);
  }
}

// Is this the stack pointer?
bool VirtualRegister::IsStackPointer(void) const {
  if (kVirtualRegisterKindUnschedulable == kind) {
    return (XED_REG_RSP == reg_num ||
            XED_REG_ESP == reg_num ||
            XED_REG_SP == reg_num ||
            XED_REG_SPL == reg_num);
  } else {
    return false;
  }
}

// Is this the instruction pointer?
bool VirtualRegister::IsInstructionPointer(void) const {
  return XED_REG_IP_FIRST <= reg_num && XED_REG_IP_LAST >= reg_num;
}

// Is this the flags register?
bool VirtualRegister::IsFlags(void) const {
  return XED_REG_FLAGS_FIRST <= reg_num && XED_REG_FLAGS_LAST >= reg_num;
}

// Update this register tracker by marking all registers that appear in an
// instruction as used.
void UsedRegisterSet::Visit(const arch::Instruction *instr) {
  GRANARY_ASSERT(XED_IFORM_INVALID != instr->iform);
  GRANARY_ASSERT(0 != instr->isel);
  for (auto i = 0; i < instr->num_ops; ++i) {
    Visit(&(instr->ops[i]));
  }
}

// Update this register tracker by marking all registers that appear in an
// instruction as used.
void UsedRegisterSet::Visit(const arch::Operand *op) {
  if (op->IsRegister()) {
    Revive(op->reg);
  } else if (op->IsMemory() && !op->IsPointer()) {
    if (op->is_compound) {
      Revive(op->mem.base);
      Revive(op->mem.index);
    } else {
      Revive(op->reg);
    }
  }
}

namespace {

// Does this instruction use legacy registers (e.g. `AH`)? If so, then this
// likely restricts the usage of REX prefixes, and therefore restricts the
// virtual register scheduler to only the original 8 GPRs.
static bool UsesLegacyRegisters(const arch::Instruction *instr) {
  for (auto i = 0U; i < instr->num_ops; ++i) {
    const auto &op(instr->ops[i]);
    if (op.IsRegister() && op.reg.IsLegacy()) return true;
  }
  return false;
}

}  // namespace

// Update this register tracker by marking some registers as used (i.e.
// restricted). This allows us to communicate some architecture-specific
// encoding constraints to the register scheduler.
void UsedRegisterSet::ReviveRestrictedRegisters(
    const arch::Instruction *instr) {
  GRANARY_ASSERT(XED_IFORM_INVALID != instr->iform);
  GRANARY_ASSERT(0 != instr->isel);

  // If legacy registers are used, then we likely can't use the extra 8
  // registers introduced by x86-64 as they require a REX prefix.
  if (GRANARY_UNLIKELY(UsesLegacyRegisters(instr))) {
    Revive(14);  // XED_REG_R15
    Revive(13);
    Revive(12);
    Revive(11);
    Revive(10);
    Revive(9);
    Revive(8);
    Revive(7);  // XED_REG_R8
  }
}

// Update this register tracker by visiting the operands of an instruction.
//
// Note: This treats conditional writes to a register as reviving that
//       register.
void LiveRegisterSet::Visit(const arch::Instruction *instr) {
  GRANARY_ASSERT(XED_IFORM_INVALID != instr->iform);
  GRANARY_ASSERT(0 != instr->isel);
  for (auto i = 0U; i < instr->num_ops; ++i) {
    Visit(&(instr->ops[i]));
  }
}

// Update this register tracker by visiting an operand of an instruction.
//
// Note: This treats conditional writes to a register as reviving that
//       register.
void LiveRegisterSet::Visit(const arch::Operand *op) {
  if (op->IsRegister()) {
    const auto &reg(op->reg);
    // Read, read/write, conditional write, or partial write.
    if (op->IsRead() || op->IsConditionalWrite() ||
        reg.PreservesBytesOnWrite()) {
      Revive(reg);
    } else if (op->IsWrite()) {  // Write-only.
      Kill(reg);
    } else {
      GRANARY_ASSERT(false);
    }
  } else if (op->IsMemory() && !op->IsPointer()) {
    if (op->is_compound) {
      Revive(op->mem.base);
      Revive(op->mem.index);
    } else {
      Revive(op->reg);
    }
  }
}

namespace arch {

VirtualRegister REG_RFLAGS;
VirtualRegister REG_EFLAGS;
VirtualRegister REG_FLAGS;

VirtualRegister REG_AX;
VirtualRegister REG_CX;
VirtualRegister REG_DX;
VirtualRegister REG_BX;
VirtualRegister REG_SP;
VirtualRegister REG_BP;
VirtualRegister REG_SI;
VirtualRegister REG_DI;
VirtualRegister REG_R8W;
VirtualRegister REG_R9W;
VirtualRegister REG_R10W;
VirtualRegister REG_R11W;
VirtualRegister REG_R12W;
VirtualRegister REG_R13W;
VirtualRegister REG_R14W;
VirtualRegister REG_R15W;
VirtualRegister REG_EAX;
VirtualRegister REG_ECX;
VirtualRegister REG_EDX;
VirtualRegister REG_EBX;
VirtualRegister REG_ESP;
VirtualRegister REG_EBP;
VirtualRegister REG_ESI;
VirtualRegister REG_EDI;
VirtualRegister REG_R8D;
VirtualRegister REG_R9D;
VirtualRegister REG_R10D;
VirtualRegister REG_R11D;
VirtualRegister REG_R12D;
VirtualRegister REG_R13D;
VirtualRegister REG_R14D;
VirtualRegister REG_R15D;
VirtualRegister REG_RAX;
VirtualRegister REG_RCX;
VirtualRegister REG_RDX;
VirtualRegister REG_RBX;
VirtualRegister REG_RSP;
VirtualRegister REG_RBP;
VirtualRegister REG_RSI;
VirtualRegister REG_RDI;
VirtualRegister REG_R8;
VirtualRegister REG_R9;
VirtualRegister REG_R10;
VirtualRegister REG_R11;
VirtualRegister REG_R12;
VirtualRegister REG_R13;
VirtualRegister REG_R14;
VirtualRegister REG_R15;
VirtualRegister REG_AL;
VirtualRegister REG_CL;
VirtualRegister REG_DL;
VirtualRegister REG_BL;
VirtualRegister REG_SPL;
VirtualRegister REG_BPL;
VirtualRegister REG_SIL;
VirtualRegister REG_DIL;
VirtualRegister REG_R8B;
VirtualRegister REG_R9B;
VirtualRegister REG_R10B;
VirtualRegister REG_R11B;
VirtualRegister REG_R12B;
VirtualRegister REG_R13B;
VirtualRegister REG_R14B;
VirtualRegister REG_R15B;
VirtualRegister REG_AH;
VirtualRegister REG_CH;
VirtualRegister REG_DH;
VirtualRegister REG_BH;
VirtualRegister REG_ERROR;
VirtualRegister REG_RIP;
VirtualRegister REG_EIP;
VirtualRegister REG_IP;
VirtualRegister REG_K0;
VirtualRegister REG_K1;
VirtualRegister REG_K2;
VirtualRegister REG_K3;
VirtualRegister REG_K4;
VirtualRegister REG_K5;
VirtualRegister REG_K6;
VirtualRegister REG_K7;
VirtualRegister REG_MMX0;
VirtualRegister REG_MMX1;
VirtualRegister REG_MMX2;
VirtualRegister REG_MMX3;
VirtualRegister REG_MMX4;
VirtualRegister REG_MMX5;
VirtualRegister REG_MMX6;
VirtualRegister REG_MMX7;
VirtualRegister REG_CS;
VirtualRegister REG_DS;
VirtualRegister REG_ES;
VirtualRegister REG_SS;
VirtualRegister REG_FS;
VirtualRegister REG_GS;
VirtualRegister REG_ST0;
VirtualRegister REG_ST1;
VirtualRegister REG_ST2;
VirtualRegister REG_ST3;
VirtualRegister REG_ST4;
VirtualRegister REG_ST5;
VirtualRegister REG_ST6;
VirtualRegister REG_ST7;
VirtualRegister REG_XCR0;
VirtualRegister REG_XMM0;
VirtualRegister REG_XMM1;
VirtualRegister REG_XMM2;
VirtualRegister REG_XMM3;
VirtualRegister REG_XMM4;
VirtualRegister REG_XMM5;
VirtualRegister REG_XMM6;
VirtualRegister REG_XMM7;
VirtualRegister REG_XMM8;
VirtualRegister REG_XMM9;
VirtualRegister REG_XMM10;
VirtualRegister REG_XMM11;
VirtualRegister REG_XMM12;
VirtualRegister REG_XMM13;
VirtualRegister REG_XMM14;
VirtualRegister REG_XMM15;
VirtualRegister REG_XMM16;
VirtualRegister REG_XMM17;
VirtualRegister REG_XMM18;
VirtualRegister REG_XMM19;
VirtualRegister REG_XMM20;
VirtualRegister REG_XMM21;
VirtualRegister REG_XMM22;
VirtualRegister REG_XMM23;
VirtualRegister REG_XMM24;
VirtualRegister REG_XMM25;
VirtualRegister REG_XMM26;
VirtualRegister REG_XMM27;
VirtualRegister REG_XMM28;
VirtualRegister REG_XMM29;
VirtualRegister REG_XMM30;
VirtualRegister REG_XMM31;
VirtualRegister REG_YMM0;
VirtualRegister REG_YMM1;
VirtualRegister REG_YMM2;
VirtualRegister REG_YMM3;
VirtualRegister REG_YMM4;
VirtualRegister REG_YMM5;
VirtualRegister REG_YMM6;
VirtualRegister REG_YMM7;
VirtualRegister REG_YMM8;
VirtualRegister REG_YMM9;
VirtualRegister REG_YMM10;
VirtualRegister REG_YMM11;
VirtualRegister REG_YMM12;
VirtualRegister REG_YMM13;
VirtualRegister REG_YMM14;
VirtualRegister REG_YMM15;
VirtualRegister REG_YMM16;
VirtualRegister REG_YMM17;
VirtualRegister REG_YMM18;
VirtualRegister REG_YMM19;
VirtualRegister REG_YMM20;
VirtualRegister REG_YMM21;
VirtualRegister REG_YMM22;
VirtualRegister REG_YMM23;
VirtualRegister REG_YMM24;
VirtualRegister REG_YMM25;
VirtualRegister REG_YMM26;
VirtualRegister REG_YMM27;
VirtualRegister REG_YMM28;
VirtualRegister REG_YMM29;
VirtualRegister REG_YMM30;
VirtualRegister REG_YMM31;
VirtualRegister REG_ZMM0;
VirtualRegister REG_ZMM1;
VirtualRegister REG_ZMM2;
VirtualRegister REG_ZMM3;
VirtualRegister REG_ZMM4;
VirtualRegister REG_ZMM5;
VirtualRegister REG_ZMM6;
VirtualRegister REG_ZMM7;
VirtualRegister REG_ZMM8;
VirtualRegister REG_ZMM9;
VirtualRegister REG_ZMM10;
VirtualRegister REG_ZMM11;
VirtualRegister REG_ZMM12;
VirtualRegister REG_ZMM13;
VirtualRegister REG_ZMM14;
VirtualRegister REG_ZMM15;
VirtualRegister REG_ZMM16;
VirtualRegister REG_ZMM17;
VirtualRegister REG_ZMM18;
VirtualRegister REG_ZMM19;
VirtualRegister REG_ZMM20;
VirtualRegister REG_ZMM21;
VirtualRegister REG_ZMM22;
VirtualRegister REG_ZMM23;
VirtualRegister REG_ZMM24;
VirtualRegister REG_ZMM25;
VirtualRegister REG_ZMM26;
VirtualRegister REG_ZMM27;
VirtualRegister REG_ZMM28;
VirtualRegister REG_ZMM29;
VirtualRegister REG_ZMM30;
VirtualRegister REG_ZMM31;

}  // namespace arch
}  // namespace granary
