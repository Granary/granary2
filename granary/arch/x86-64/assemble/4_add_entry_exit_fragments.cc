/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/instruction.h"

#include "granary/code/assemble/fragment.h"

namespace granary {
namespace arch {

// Table mapping each iclass to the set of read and written flags by *any*
// selection of that iclass.
extern const FlagsSet IFORM_FLAGS[];

}  // namespace arch

// Visits an instructions within the fragment and revives/kills architecture-
// specific flags stored in the `FlagUsageInfo` object.
void VisitInstructionFlags(const arch::Instruction &instr,
                           FlagUsageInfo *flags) {
  auto &instr_flags(arch::IFORM_FLAGS[instr.iform]);
  flags->all_written_flags |= instr_flags.written.flat;
  flags->all_read_flags |= instr_flags.read.flat;
  flags->entry_live_flags &= ~instr_flags.written.flat;
  flags->entry_live_flags |= instr_flags.read.flat;
}

}  // namespace granary

