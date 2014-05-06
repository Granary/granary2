/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/builder.h"
#include "granary/arch/x86-64/instruction.h"

#include "granary/cfg/instruction.h"

#include "granary/code/assemble/fragment.h"

#include "granary/code/register.h"

// Append a non-native, created instruction to the fragment.
#define APP(...) \
  do { \
    __VA_ARGS__; \
    auto ninstr = new NativeInstruction(&ni); \
    frag->instrs.Append(ninstr); \
  } while (0)

// Prepend a non-native, created instruction to the fragment.
#define PREP(...) \
  do { \
    __VA_ARGS__; \
    auto ninstr = new NativeInstruction(&ni); \
    frag->instrs.Prepend(ninstr); \
  } while (0)

namespace granary {

// Returns the architectural register that is potentially killed by the
// instructions injected to save/restore flags.
//
// Note: This must return a register with width `arch::GPR_WIDTH_BYTES` if the
//       returned register is valid.
VirtualRegister FlagKillReg(void) {
  return VirtualRegister::FromNative(XED_REG_RAX);
}

// Inserts instructions that saves the flags within the fragment `frag`.
void InjectSaveFlags(Fragment *frag) {
  arch::Instruction ni;
  auto zone = frag->flag_zone.Value();
  if (1 == zone->num_frags_in_zone) {
    frag = zone->only_frag;
  }
  xed_flag_set_t flags;
  flags.flat = zone->killed_flags & zone->live_flags;
  if (flags.flat) {
    GRANARY_IF_DEBUG( xed_flag_set_t killed_flags; )
    GRANARY_IF_DEBUG( killed_flags.flat = zone->killed_flags; )
    GRANARY_ASSERT(!killed_flags.s.df);
    if (flags.s.of) {
      PREP(SETO_GPR8(&ni, XED_REG_AL));
    }
    PREP(LAHF(&ni));
    if (zone->flag_killed_reg.IsNative() &&
        zone->live_regs.IsLive(zone->flag_killed_reg.Number())) {
      PREP(MOV_GPRv_GPRv_89(&ni, zone->flag_save_reg, zone->flag_killed_reg));
    }
  }
}

// Inserts instructions that restore the flags within the fragment `frag`.
void InjectRestoreFlags(Fragment *frag) {
  arch::Instruction ni;
  auto zone = frag->flag_zone.Value();
  if (1 == zone->num_frags_in_zone) {
    frag = zone->only_frag;
  }
  xed_flag_set_t flags;
  flags.flat = zone->killed_flags & zone->live_flags;
  if (flags.flat) {
    if (flags.s.of) {
      APP(ADD_GPR8_IMMb_80r0(&ni, XED_REG_AL, 0x7F));
    }
    APP(SAHF(&ni));
    if (zone->flag_killed_reg.IsNative() &&
        zone->live_regs.IsLive(zone->flag_killed_reg.Number())) {
      APP(MOV_GPRv_GPRv_89(&ni, zone->flag_killed_reg, zone->flag_save_reg));
    }
  }
}

}  // namespace granary
