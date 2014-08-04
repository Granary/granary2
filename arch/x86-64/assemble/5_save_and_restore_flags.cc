/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/builder.h"
#include "arch/x86-64/instruction.h"

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"
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
namespace arch {

// Returns the architectural register that is potentially killed by the
// instructions injected to save/restore flags.
//
// Note: This must return a register with width `arch::GPR_WIDTH_BYTES` if the
//       returned register is valid.
VirtualRegister FlagKillReg(void) {
  return VirtualRegister::FromNative(XED_REG_RAX);
}

namespace {

struct SaveRestore {
  // The `flag_killed_reg` is an arch GPR (RAX on Intel) that is clobbered by
  // whatever instructions are required to save the flags. If the flag killed
  // reg is live after a flag zone, or live inside of a flag zone, then we
  // need to save its value so that we can restore it.
  bool save_flag_killed_reg;

  // `true` if the flag killed reg is used inside of a flag zone. If this is
  // true, and if we need to save the flags, then this implies that the flag
  // killed reg needs to be saved before saving the flags, and restored
  // immediately after saving the flags.
  bool killed_reg_used;
};

static SaveRestore SaveRestoreKilledReg(const FlagZone *zone) {
  SaveRestore ret = {false, false};

  // Flag killed reg is live after the flag zone.
  if (zone->flag_killed_reg.IsNative() &&
      zone->live_regs.IsLive(zone->flag_killed_reg.Number())) {
    ret.save_flag_killed_reg = true;
  }

  // Flag killed reg is used in the flag zone.
  if ((ret.killed_reg_used = zone->used_regs.IsLive(zone->flag_killed_reg))) {
    ret.save_flag_killed_reg = true;
  }
  return ret;
}

}  // namespace

// Inserts instructions that saves the flags within the fragment `frag`.
void InjectSaveFlags(Fragment *frag) {
  arch::Instruction ni;
  auto zone = frag->flag_zone.Value();
  xed_flag_set_t flags;
  flags.flat = zone->killed_flags & zone->live_flags;
  if (!flags.flat) return;

  GRANARY_IF_DEBUG( xed_flag_set_t killed_flags; )
  GRANARY_IF_DEBUG( killed_flags.flat = zone->killed_flags; )
  GRANARY_ASSERT(!killed_flags.s.df);
  GRANARY_ASSERT(zone->flag_save_reg != zone->flag_killed_reg);

  const auto sr = SaveRestoreKilledReg(zone);

  // Step 4: Restore the native version of `flag_killed_reg` from
  // `flag_save_reg`, while also keeping the flags state alive in
  // `flag_killed_reg`.
  if (sr.save_flag_killed_reg && sr.killed_reg_used) {
    PREP(XCHG_GPRv_GPRv(&ni, zone->flag_save_reg, zone->flag_killed_reg));
  }

  if (flags.s.of) {  // Step 3: Save the overflow flag.
    PREP(SETO_GPR8(&ni, XED_REG_AL));
  }

  PREP(LAHF(&ni));  // Step 2: Save the arithmetic flags.

  // Step 1: Save the native version of `flag_killed_reg` into `flag_save_reg`.
  if (sr.save_flag_killed_reg) {
    PREP(MOV_GPRv_GPRv_89(&ni, zone->flag_save_reg, zone->flag_killed_reg);
         ni.is_save_restore = true; );
  }
}

// Inserts instructions that restore the flags within the fragment `frag`.
void InjectRestoreFlags(Fragment *frag) {
  arch::Instruction ni;
  auto zone = frag->flag_zone.Value();
  xed_flag_set_t flags;
  flags.flat = zone->killed_flags & zone->live_flags;
  if (!flags.flat) return;
  const auto sr = SaveRestoreKilledReg(zone);

  // Step 1: Extract the saved flags from `flag_save_reg`, while keeping
  // the value of `flag_killed_reg` alive.
  if (sr.save_flag_killed_reg && sr.killed_reg_used) {
    APP(XCHG_GPRv_GPRv(&ni, zone->flag_save_reg, zone->flag_killed_reg));
  }

  // Step 2: Restore the overflow flag.
  if (flags.s.of) {
    APP(ADD_GPR8_IMMb_80r0(&ni, XED_REG_AL, static_cast<uint8_t>(0x7F)));
  }

  // Step 3: Restore the flags.
  APP(SAHF(&ni));

  // Step 4: Restore the native value of `flag_killed_reg`
  if (sr.save_flag_killed_reg) {
    APP(
      MOV_GPRv_GPRv_89(&ni, zone->flag_killed_reg, zone->flag_save_reg);
      ni.is_save_restore = true; );
  }
}

}  // namespace arch
}  // namespace granary
