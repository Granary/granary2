/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"

#include "granary/arch/x86-64/xed.h"

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

  auto reg = static_cast<xed_reg_enum_t>(reg_);
  auto widest_reg = xed_get_largest_enclosing_register(reg);
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
  kind = VR_KIND_ARCH_VIRTUAL;
  reg_num = widest_reg - XED_REG_RAX;
  if (XED_REG_RSP <= widest_reg) {
    reg_num -= 1;  // Directly map registers to indexes.
  }
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
  } else if (VR_KIND_ARCH_VIRTUAL != kind) {
    return XED_REG_INVALID;
  }

  // Map register numbers to XED registers.
  auto widest_reg = static_cast<xed_reg_enum_t>(reg_num + XED_REG_RAX);
  if (XED_REG_RSP <= widest_reg) {
    widest_reg = static_cast<xed_reg_enum_t>(static_cast<int>(widest_reg) + 1);
  }

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
      num_bytes = 5;
      byte_mask = ALL_8_BYTES;
      preserved_byte_mask = 0;
      return;
    default: GRANARY_ASSERT(false);
  }
}

// Is this the stack pointer?
bool VirtualRegister::IsStackPointer(void) const {
  return XED_REG_RSP == reg_num || XED_REG_ESP == reg_num ||
         XED_REG_SP == reg_num || XED_REG_SPL == reg_num;
}

// Is this the instruction pointer?
bool VirtualRegister::IsInstructionPointer(void) const {
  return XED_REG_IP_FIRST <= reg_num && XED_REG_IP_LAST >= reg_num;
}

// Is this the flags register?
bool VirtualRegister::IsFlags(void) const {
  return XED_REG_FLAGS_FIRST <= reg_num && XED_REG_FLAGS_LAST >= reg_num;
}

}  // namespace granary
