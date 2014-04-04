/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"

#include "granary/code/assemble/fragment.h"

// TODO(pag): Implement "smart" saving/restoring of flags. For example:
//
//                  <instrumentation I1, kills F1>
//                  <native, doesn't r/w flags>
//                  <instrumentation I2, kills F1>
//                  <native, reads F1>
//
//            Then we could be clever about saving F1 before I1 and restoring
//            F1 after I2 if and only if I1 and I2 are within the same fragment
//            partition.

namespace granary {

// Visits all native instructions within the fragment and kills any flags that
// those instructions kill. This does not revive any flags.
//
// Note: This has an architecture-specific implementation.
void KillFragmentFlags(Fragment * const frag);

// Visits a native instructions within the fragment and revives/kills
// flags.
//
// Note: This has an architecture-specific implementation.
uint32_t VisitInstructionFlags(const NativeInstruction *instr,
                               uint32_t in_flags);

// Inserts instructions that saves the flags within the fragment `frag`.
//
// Note: This has an architecture-specific implementation.
void InjectSaveFlags(LocalControlFlowGraph *cfg, Fragment *frag);

// Inserts instructions that restore the flags within the fragment `frag`.
//
// Note: This has an architecture-specific implementation.
void InjectRestoreFlags(LocalControlFlowGraph *cfg, Fragment *frag);

namespace {

// Scan each decoded basic block to determine the set of
static void InitFragmentFlagsUse(Fragment * const frags) {
  for (auto frag : FragmentIterator(frags)) {
    frag->inst_killed_flags = 0U;
    frag->app_live_flags = ~0U;
    frag->transient_back_link = nullptr;
    frag->transient_virt_reg_num = -1;
    if (!frag->is_exit && !frag->is_future_block_head) {
      if (FRAG_KIND_INSTRUMENTATION == frag->kind) {
        KillFragmentFlags(frag);
      } else if (FRAG_KIND_FLAG_ENTRY == frag->kind) {
        frag->transient_back_link = frag;
      }
    }
  }
}

// Returns the set of application flags that are live on entry to a fragment.
static uint32_t LiveAppFlags(const Fragment *frag) {
  return frag ? frag->app_live_flags : ~0U;
}

// Find the set of live application flags on entry to every fragment.
static void FindLiveAppFlags(Fragment * const frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : FragmentIterator(frags)) {
      auto exit_flags = LiveAppFlags(frag->fall_through_target) |
                        LiveAppFlags(frag->branch_target);
      auto old_entry_flags = frag->app_live_flags;
      if (FRAG_KIND_APPLICATION == frag->kind) {
        for (auto instr : BackwardInstructionIterator(frag->last)) {
          exit_flags = VisitInstructionFlags(
              DynamicCast<NativeInstruction *>(instr), exit_flags);
        }
      }
      if (exit_flags != old_entry_flags) {
        changed = true;
        frag->app_live_flags = exit_flags;
      }
    }
  }
}

// Forward and backward propagate the instrumentation-killed flags within
// regions of instrumented code. This will also propagate a "designated" flag
// entry fragment for each flag save/restore zone.
static bool PropagateInstKilledFlags(Fragment *frag, Fragment *succ) {
  if (succ && frag->partition_id == succ->partition_id &&
      FRAG_KIND_FLAG_ENTRY != succ->kind && FRAG_KIND_FLAG_EXIT != frag->kind) {
    auto bl = std::max(frag->transient_back_link, succ->transient_back_link);
    if (frag->inst_killed_flags != succ->inst_killed_flags ||
        frag->transient_back_link != bl ||
        succ->transient_back_link != bl) {
      auto flags = frag->inst_killed_flags | succ->inst_killed_flags;
      frag->inst_killed_flags = flags;
      succ->inst_killed_flags = flags;
      frag->transient_back_link = bl;
      succ->transient_back_link = bl;
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

// Find the set of killed instrumentation flags in every flag entry/exit
// fragment. The key is that the set of instrumentation-killed flags match in
// both the `FRAG_KIND_FLAG_ENTRY` and `FRAG_KIND_FLAG_EXIT` blocks. This
// requires both forward and backward data-flow propagation.
static void FindEntryExitKilledFlags(Fragment * const frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : FragmentIterator(frags)) {
      auto c1 = PropagateInstKilledFlags(frag, frag->fall_through_target);
      auto c2 = PropagateInstKilledFlags(frag, frag->branch_target);
      changed = changed || c1 || c2;
    }
  }
}

// Inserts instructions that save and restore the flags around instrumentation
// code.
static void InjectFlagSavesAndRestores(LocalControlFlowGraph *cfg,
                                       Fragment * const frags) {
  for (auto frag : FragmentIterator(frags)) {
    if (FRAG_KIND_FLAG_ENTRY == frag->kind) {
      if (frag->inst_killed_flags & frag->app_live_flags) {
        InjectSaveFlags(cfg, frag);
      }
    } else if (FRAG_KIND_FLAG_EXIT == frag->kind) {
      if (frag->inst_killed_flags & frag->app_live_flags) {
        InjectRestoreFlags(cfg, frag);
      }
    }
  }
}

}  // namespace

// Insert flags saving code into `FRAG_TYPE_FLAG_ENTRY` fragments, and flag
// restoring code into `FRAG_TYPE_FLAG_EXIT` code. We only insert code to save
// and restore flags if it is necessary.
void SaveAndRestoreFlags(LocalControlFlowGraph *cfg, Fragment * const frags) {
  InitFragmentFlagsUse(frags);
  FindLiveAppFlags(frags);
  FindEntryExitKilledFlags(frags);
  InjectFlagSavesAndRestores(cfg, frags);
}

}  // namespace granary
