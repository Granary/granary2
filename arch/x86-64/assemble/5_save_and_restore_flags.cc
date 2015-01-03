/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/builder.h"
#include "arch/x86-64/instruction.h"
#include "arch/x86-64/register.h"

#include "granary/base/option.h"

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"
#include "granary/code/register.h"

GRANARY_DEFINE_bool(always_spill_flags, false,
    "Should the flags always be saved/restored any time instrumentation code "
    "writes to the flags, regardless of whether or not it seems like the "
    "application code will kill those flags. The default value is `no`.\n"
    "\n"
    "Note: Enabling this is a useful way of testing whether or not Granary\n"
    "      is correctly tracking, saving, and restoring the native flags\n"
    "      state between interleaved sections of app and client code.");

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

// Inserts instructions that saves the flags within the fragment `frag`.
void InjectSaveFlags(Fragment *frag) {
  arch::Instruction ni;
  auto zone = frag->flag_zone.Value();
  xed_flag_set_t flags;

  flags.flat = zone.killed_flags;
  if (!FLAG_always_spill_flags) flags.flat &= zone.live_flags;
  if (!flags.flat) return;

  GRANARY_IF_DEBUG( xed_flag_set_t killed_flags; )
  GRANARY_IF_DEBUG( killed_flags.flat = zone.killed_flags; )
  GRANARY_ASSERT(!killed_flags.s.df);

  // Step 4: Restore RAX.
  frag->instrs.Prepend(new AnnotationInstruction(
      kAnnotSwapRestoreRegister, REG_RAX));

  // Step 3: Save the overflow flag.
  if (flags.s.of) PREP(SETO_GPR8(&ni, XED_REG_AL));

  // Step 2: Save the arithmetic flags.
  PREP(LAHF(&ni));

  // Step 1: Save the native version of `flag_killed_reg` into `flag_save_reg`.
  frag->instrs.Prepend(new AnnotationInstruction(kAnnotSaveRegister, REG_RAX));
}

// Inserts instructions that restore the flags within the fragment `frag`.
void InjectRestoreFlags(Fragment *frag) {
  arch::Instruction ni;
  auto zone = frag->flag_zone.Value();
  xed_flag_set_t flags;
  flags.flat = zone.killed_flags;
  if (!FLAG_always_spill_flags) flags.flat &= zone.live_flags;
  if (!flags.flat) return;

  // Step 1: Extract the saved flags from `flag_save_reg`, while keeping
  // the value of `flag_killed_reg` alive.
  frag->instrs.Prepend(new AnnotationInstruction(kAnnotSwapRestoreRegister,
                                                 REG_RAX));
  // Step 2: Restore the overflow flag.
  if (flags.s.of) {
    APP(ADD_GPR8_IMMb_80r0(&ni, XED_REG_AL, static_cast<uint8_t>(0x7F)));
  }

  // Step 3: Restore the flags.
  APP(SAHF(&ni));

  // Step 4: Restore the native value of `flag_killed_reg`
  frag->instrs.Append(new AnnotationInstruction(kAnnotRestoreRegister,
                                                REG_RAX));
}

}  // namespace arch
}  // namespace granary
