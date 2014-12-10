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
void VirtualRegister::DecodeFromNative(int reg_) {
  value = 0;  // Reset.

  const auto reg = static_cast<xed_reg_enum_t>(reg_);
  if (XED_REG_INVALID == reg || XED_REG_LAST <= reg_) {
    kind = VR_KIND_UNKNOWN;
    return;
  }

  const auto widest_reg = xed_get_largest_enclosing_register(reg);
  num_bytes = static_cast<uint8_t>(xed_get_register_width_bits64(reg) / 8);

  // Non-general-purpose registers are treated as "fixed" architectural
  // registers.
  if (XED_REG_RAX > widest_reg || XED_REG_R15 < widest_reg ||
      XED_REG_RSP == widest_reg) {
    kind = VR_KIND_ARCH_FIXED;
    reg_num = static_cast<decltype(reg_num)>(reg_);
    return;
  }

  // General-purpose registers are disambiguated in terms of their "widest"
  // enclosing register, and then specialized in terms of their width and which
  // bytes are actually named by the register.
  kind = VR_KIND_ARCH_GPR;
  reg_num = widest_reg - XED_REG_RAX;
  if (XED_REG_RSP <= widest_reg) {
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
int VirtualRegister::EncodeToNative(void) const {
  if (VR_KIND_ARCH_FIXED == kind) {
    return static_cast<int>(reg_num);
  } else if (VR_KIND_ARCH_GPR != kind) {
    return XED_REG_INVALID;
  }

  // Map register numbers to XED registers.
  auto widest_reg = static_cast<xed_reg_enum_t>(reg_num + XED_REG_RAX);
  if (XED_REG_RSP <= widest_reg) {
    widest_reg = static_cast<xed_reg_enum_t>(static_cast<int>(widest_reg) + 1);
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
  return VirtualRegister::FromNative(static_cast<int>(XED_REG_RFLAGS));
}

// Return the instruction pointer register as a virtual register.
//
// Note: This has an architecture-specific implementation.
VirtualRegister VirtualRegister::InstructionPointer(void) {
  return VirtualRegister::FromNative(static_cast<int>(XED_REG_RIP));
}

// Return the stack pointer register as a virtual register.
//
// Note: This has an architecture-specific implementation.
VirtualRegister VirtualRegister::StackPointer(void) {
  return VirtualRegister::FromNative(static_cast<int>(XED_REG_RSP));
}

// Return the frame pointer register as a virtual register.
//
// Note: This has an architecture-specific implementation.
VirtualRegister VirtualRegister::FramePointer(void) {
  // TODO(pag): This has an ABI-specific implementation.
  return VirtualRegister::FromNative(static_cast<int>(XED_REG_RBP));
}

// Widen this virtual register to a specific bit width.
void VirtualRegister::Widen(int dest_byte_width) {
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
  if (VR_KIND_ARCH_FIXED == kind) {
    return XED_REG_RSP == reg_num || XED_REG_ESP == reg_num ||
           XED_REG_SP == reg_num || XED_REG_SPL == reg_num;
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
  for (auto &op : instr->ops) {
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
  for (auto i = 0; i < instr->num_ops; ++i) {
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

const VirtualRegister REG_AX GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_AX));
const VirtualRegister REG_CX GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_CX));
const VirtualRegister REG_DX GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_DX));
const VirtualRegister REG_BX GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_BX));
const VirtualRegister REG_SP GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_SP));
const VirtualRegister REG_BP GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_BP));
const VirtualRegister REG_SI GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_SI));
const VirtualRegister REG_DI GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_DI));
const VirtualRegister REG_R8W GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R8W));
const VirtualRegister REG_R9W GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R9W));
const VirtualRegister REG_R10W GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R10W));
const VirtualRegister REG_R11W GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R11W));
const VirtualRegister REG_R12W GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R12W));
const VirtualRegister REG_R13W GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R13W));
const VirtualRegister REG_R14W GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R14W));
const VirtualRegister REG_R15W GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R15W));
const VirtualRegister REG_EAX GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_EAX));
const VirtualRegister REG_ECX GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ECX));
const VirtualRegister REG_EDX GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_EDX));
const VirtualRegister REG_EBX GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_EBX));
const VirtualRegister REG_ESP GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ESP));
const VirtualRegister REG_EBP GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_EBP));
const VirtualRegister REG_ESI GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ESI));
const VirtualRegister REG_EDI GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_EDI));
const VirtualRegister REG_R8D GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R8D));
const VirtualRegister REG_R9D GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R9D));
const VirtualRegister REG_R10D GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R10D));
const VirtualRegister REG_R11D GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R11D));
const VirtualRegister REG_R12D GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R12D));
const VirtualRegister REG_R13D GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R13D));
const VirtualRegister REG_R14D GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R14D));
const VirtualRegister REG_R15D GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R15D));
const VirtualRegister REG_RAX GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_RAX));
const VirtualRegister REG_RCX GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_RCX));
const VirtualRegister REG_RDX GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_RDX));
const VirtualRegister REG_RBX GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_RBX));
const VirtualRegister REG_RSP GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_RSP));
const VirtualRegister REG_RBP GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_RBP));
const VirtualRegister REG_RSI GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_RSI));
const VirtualRegister REG_RDI GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_RDI));
const VirtualRegister REG_R8 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R8));
const VirtualRegister REG_R9 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R9));
const VirtualRegister REG_R10 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R10));
const VirtualRegister REG_R11 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R11));
const VirtualRegister REG_R12 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R12));
const VirtualRegister REG_R13 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R13));
const VirtualRegister REG_R14 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R14));
const VirtualRegister REG_R15 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R15));
const VirtualRegister REG_AL GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_AL));
const VirtualRegister REG_CL GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_CL));
const VirtualRegister REG_DL GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_DL));
const VirtualRegister REG_BL GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_BL));
const VirtualRegister REG_SPL GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_SPL));
const VirtualRegister REG_BPL GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_BPL));
const VirtualRegister REG_SIL GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_SIL));
const VirtualRegister REG_DIL GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_DIL));
const VirtualRegister REG_R8B GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R8B));
const VirtualRegister REG_R9B GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R9B));
const VirtualRegister REG_R10B GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R10B));
const VirtualRegister REG_R11B GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R11B));
const VirtualRegister REG_R12B GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R12B));
const VirtualRegister REG_R13B GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R13B));
const VirtualRegister REG_R14B GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R14B));
const VirtualRegister REG_R15B GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_R15B));
const VirtualRegister REG_AH GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_AH));
const VirtualRegister REG_CH GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_CH));
const VirtualRegister REG_DH GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_DH));
const VirtualRegister REG_BH GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_BH));
const VirtualRegister REG_ERROR GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ERROR));
const VirtualRegister REG_RIP GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_RIP));
const VirtualRegister REG_EIP GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_EIP));
const VirtualRegister REG_IP GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_IP));
const VirtualRegister REG_K0 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_K0));
const VirtualRegister REG_K1 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_K1));
const VirtualRegister REG_K2 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_K2));
const VirtualRegister REG_K3 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_K3));
const VirtualRegister REG_K4 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_K4));
const VirtualRegister REG_K5 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_K5));
const VirtualRegister REG_K6 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_K6));
const VirtualRegister REG_K7 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_K7));
const VirtualRegister REG_MMX0 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_MMX0));
const VirtualRegister REG_MMX1 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_MMX1));
const VirtualRegister REG_MMX2 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_MMX2));
const VirtualRegister REG_MMX3 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_MMX3));
const VirtualRegister REG_MMX4 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_MMX4));
const VirtualRegister REG_MMX5 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_MMX5));
const VirtualRegister REG_MMX6 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_MMX6));
const VirtualRegister REG_MMX7 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_MMX7));
const VirtualRegister REG_CS GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_CS));
const VirtualRegister REG_DS GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_DS));
const VirtualRegister REG_ES GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ES));
const VirtualRegister REG_SS GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_SS));
const VirtualRegister REG_FS GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_FS));
const VirtualRegister REG_GS GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_GS));
const VirtualRegister REG_ST0 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ST0));
const VirtualRegister REG_ST1 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ST1));
const VirtualRegister REG_ST2 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ST2));
const VirtualRegister REG_ST3 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ST3));
const VirtualRegister REG_ST4 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ST4));
const VirtualRegister REG_ST5 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ST5));
const VirtualRegister REG_ST6 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ST6));
const VirtualRegister REG_ST7 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ST7));
const VirtualRegister REG_XCR0 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XCR0));
const VirtualRegister REG_XMM0 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM0));
const VirtualRegister REG_XMM1 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM1));
const VirtualRegister REG_XMM2 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM2));
const VirtualRegister REG_XMM3 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM3));
const VirtualRegister REG_XMM4 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM4));
const VirtualRegister REG_XMM5 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM5));
const VirtualRegister REG_XMM6 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM6));
const VirtualRegister REG_XMM7 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM7));
const VirtualRegister REG_XMM8 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM8));
const VirtualRegister REG_XMM9 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM9));
const VirtualRegister REG_XMM10 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM10));
const VirtualRegister REG_XMM11 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM11));
const VirtualRegister REG_XMM12 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM12));
const VirtualRegister REG_XMM13 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM13));
const VirtualRegister REG_XMM14 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM14));
const VirtualRegister REG_XMM15 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM15));
const VirtualRegister REG_XMM16 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM16));
const VirtualRegister REG_XMM17 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM17));
const VirtualRegister REG_XMM18 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM18));
const VirtualRegister REG_XMM19 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM19));
const VirtualRegister REG_XMM20 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM20));
const VirtualRegister REG_XMM21 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM21));
const VirtualRegister REG_XMM22 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM22));
const VirtualRegister REG_XMM23 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM23));
const VirtualRegister REG_XMM24 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM24));
const VirtualRegister REG_XMM25 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM25));
const VirtualRegister REG_XMM26 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM26));
const VirtualRegister REG_XMM27 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM27));
const VirtualRegister REG_XMM28 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM28));
const VirtualRegister REG_XMM29 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM29));
const VirtualRegister REG_XMM30 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM30));
const VirtualRegister REG_XMM31 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_XMM31));
const VirtualRegister REG_YMM0 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM0));
const VirtualRegister REG_YMM1 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM1));
const VirtualRegister REG_YMM2 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM2));
const VirtualRegister REG_YMM3 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM3));
const VirtualRegister REG_YMM4 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM4));
const VirtualRegister REG_YMM5 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM5));
const VirtualRegister REG_YMM6 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM6));
const VirtualRegister REG_YMM7 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM7));
const VirtualRegister REG_YMM8 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM8));
const VirtualRegister REG_YMM9 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM9));
const VirtualRegister REG_YMM10 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM10));
const VirtualRegister REG_YMM11 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM11));
const VirtualRegister REG_YMM12 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM12));
const VirtualRegister REG_YMM13 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM13));
const VirtualRegister REG_YMM14 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM14));
const VirtualRegister REG_YMM15 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM15));
const VirtualRegister REG_YMM16 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM16));
const VirtualRegister REG_YMM17 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM17));
const VirtualRegister REG_YMM18 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM18));
const VirtualRegister REG_YMM19 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM19));
const VirtualRegister REG_YMM20 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM20));
const VirtualRegister REG_YMM21 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM21));
const VirtualRegister REG_YMM22 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM22));
const VirtualRegister REG_YMM23 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM23));
const VirtualRegister REG_YMM24 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM24));
const VirtualRegister REG_YMM25 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM25));
const VirtualRegister REG_YMM26 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM26));
const VirtualRegister REG_YMM27 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM27));
const VirtualRegister REG_YMM28 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM28));
const VirtualRegister REG_YMM29 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM29));
const VirtualRegister REG_YMM30 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM30));
const VirtualRegister REG_YMM31 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_YMM31));
const VirtualRegister REG_ZMM0 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM0));
const VirtualRegister REG_ZMM1 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM1));
const VirtualRegister REG_ZMM2 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM2));
const VirtualRegister REG_ZMM3 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM3));
const VirtualRegister REG_ZMM4 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM4));
const VirtualRegister REG_ZMM5 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM5));
const VirtualRegister REG_ZMM6 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM6));
const VirtualRegister REG_ZMM7 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM7));
const VirtualRegister REG_ZMM8 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM8));
const VirtualRegister REG_ZMM9 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM9));
const VirtualRegister REG_ZMM10 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM10));
const VirtualRegister REG_ZMM11 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM11));
const VirtualRegister REG_ZMM12 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM12));
const VirtualRegister REG_ZMM13 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM13));
const VirtualRegister REG_ZMM14 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM14));
const VirtualRegister REG_ZMM15 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM15));
const VirtualRegister REG_ZMM16 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM16));
const VirtualRegister REG_ZMM17 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM17));
const VirtualRegister REG_ZMM18 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM18));
const VirtualRegister REG_ZMM19 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM19));
const VirtualRegister REG_ZMM20 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM20));
const VirtualRegister REG_ZMM21 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM21));
const VirtualRegister REG_ZMM22 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM22));
const VirtualRegister REG_ZMM23 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM23));
const VirtualRegister REG_ZMM24 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM24));
const VirtualRegister REG_ZMM25 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM25));
const VirtualRegister REG_ZMM26 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM26));
const VirtualRegister REG_ZMM27 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM27));
const VirtualRegister REG_ZMM28 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM28));
const VirtualRegister REG_ZMM29 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM29));
const VirtualRegister REG_ZMM30 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM30));
const VirtualRegister REG_ZMM31 GRANARY_GLOBAL =
    VirtualRegister::FromNative(static_cast<int>(XED_REG_ZMM31));

}  // namespace arch
}  // namespace granary
