/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/option.h"

#include "granary/code/assemble.h"
#include "granary/code/assemble/fragment.h"

// Stages of assembly.
#include "granary/code/assemble/0_compile_inline_assembly.h"
#include "granary/code/assemble/1_relativize.h"
#include "granary/code/assemble/2_build_fragment_list.h"
#include "granary/code/assemble/3_partition_fragments.h"
#include "granary/code/assemble/4_add_entry_exit_fragments.h"
#include "granary/code/assemble/5_save_and_restore_flags.h"
#include "granary/code/assemble/6_track_ssa_vars.h"
/*
#include "granary/code/assemble/7_propagate_copies.h"
#include "granary/code/assemble/8_schedule_registers.h"
*/
#include "granary/code/assemble/9_log_fragments.h"

#include "granary/logging.h"
#include "granary/util.h"

GRANARY_DEFINE_bool(debug_log_assembled_fragments, false,
    "Log the assembled fragments before doing final linking. The default is "
    "false.");

namespace granary {

// Assemble the local control-flow graph.
void Assemble(ContextInterface* env, CodeCacheInterface *code_cache,
              LocalControlFlowGraph *cfg) {

  // Compile all inline assembly instructions by parsing the inline assembly
  // instructions and doing code generation for them.
  CompileInlineAssembly(cfg);

  // "Fix" instructions that might use PC-relative operands that are now too
  // far away from their original data/targets (e.g. if the code cache is really
  // far away from the original native code in memory).
  RelativizeLCFG(code_cache, cfg);

  FragmentList frags;

  // Split the LCFG into fragments. The relativization step might introduce its
  // own control flow, as well as instrumentation tools. This means that
  // `DecodedBasicBlock`s no longer represent "true" basic blocks because they
  // can contain internal control-flow. This makes further analysis more
  // complicated, so to simplify things we re-split up the blocks into fragments
  // that represent the "true" basic blocks.
  BuildFragmentList(cfg, &frags);

  // Try to figure out the stack frame size on entry to / exit from every
  // fragment.
  PartitionFragments(&frags);

  // Add a bunch of entry/exit fragments at places where flags needs to be
  // saved/restored, and at places where GPRs need to be spilled / filled.
  AddEntryAndExitFragments(&frags);

  // Add flags saving and restoring code around injected instrumentation
  // instructions.
  SaveAndRestoreFlags(cfg, &frags);

  TrackSSAVars(&frags);
  /*
  // Build an SSA-like representation for the virtual register definitions.


  // Perform a single step of copy propagation. The purpose of this is to
  // allow us to eventually get rid of
  PropagateRegisterCopies(frags);

  // Schedule the virtual registers into either physical registers or memory
  // locations.
  ScheduleRegisters(frags);
   */
  if (FLAG_debug_log_assembled_fragments) {
    Log(LogDebug, &frags);
  }


  GRANARY_UNUSED(env);
}

}  // namespace granary
