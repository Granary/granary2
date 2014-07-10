/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/option.h"

#include "granary/code/assemble.h"
#include "granary/code/fragment.h"

// Stages of assembly.
#include "granary/code/assemble/0_compile_inline_assembly.h"
#include "granary/code/assemble/1_mangle.h"
#include "granary/code/assemble/2_build_fragment_list.h"
#include "granary/code/assemble/3_partition_fragments.h"
#include "granary/code/assemble/4_add_entry_exit_fragments.h"
#include "granary/code/assemble/5_save_and_restore_flags.h"
#include "granary/code/assemble/6_track_ssa_vars.h"
#include "granary/code/assemble/7_propagate_copies.h"
#include "granary/code/assemble/8_schedule_registers.h"
#include "granary/code/assemble/9_allocate_slots.h"
#include "granary/code/assemble/10_add_connecting_jumps.h"

#include "granary/logging.h"
#include "granary/util.h"

GRANARY_DEFINE_bool(debug_log_fragments, false,
    "Log the assembled fragments before doing final linking. The default is "
    "false.");

GRANARY_DEFINE_positive_int(copy_propagations, 1,
    "The number of iterations of copy propagation to run. The default is 1.");

namespace granary {

// Assemble the local control-flow graph.
FragmentList Assemble(ContextInterface *context, CodeCache *code_cache,
                      LocalControlFlowGraph *cfg) {

  // Compile all inline assembly instructions by parsing the inline assembly
  // instructions and doing code generation for them.
  CompileInlineAssembly(cfg);

  // "Fix" instructions that might use PC-relative operands that are now too
  // far away from their original data/targets (e.g. if the code cache is really
  // far away from the original native code in memory).
  MangleInstructions(code_cache, cfg);

  FragmentList frags;

  // Split the LCFG into fragments. The relativization step might introduce its
  // own control flow, as well as instrumentation tools. This means that
  // `DecodedBasicBlock`s no longer represent "true" basic blocks because they
  // can contain internal control-flow. This makes further analysis more
  // complicated, so to simplify things we re-split up the blocks into fragments
  // that represent the "true" basic blocks.
  BuildFragmentList(context, cfg, &frags);

  // Try to figure out the stack frame size on entry to / exit from every
  // fragment.
  PartitionFragments(&frags);

  // Add a bunch of entry/exit fragments at places where flags needs to be
  // saved/restored, and at places where GPRs need to be spilled / filled.
  AddEntryAndExitFragments(&frags);

  // Add flags saving and restoring code around injected instrumentation
  // instructions.
  SaveAndRestoreFlags(cfg, &frags);

  // Build an SSA-like representation for all definitions and uses of general-
  // purpose registers.
  TrackSSAVars(&frags);

  // Perform a single step of copy propagation. The purpose of this is to
  // allow us to get rid of redundant defs/uses of registers that are created
  // by earlier steps.
  for (auto i = 0; i < FLAG_copy_propagations; ++i) {
    PropagateRegisterCopies(&frags);
  }

  // Schedule the virtual registers into either physical registers or memory
  // locations.
  ScheduleRegisters(&frags);

  // Allocate space for the virtual registers, and perform final mangling of
  // instructions so that all abstract spill slots are converted into concrete
  // spill slots.
  AllocateSlots(&frags);

  // Add final connecting jumps (where needed) between predecessor and
  // successor fragments.
  AddConnectingJumps(&frags);

  if (FLAG_debug_log_fragments) {
    Log(LogDebug, &frags);
  }

  return frags;
}

}  // namespace granary
