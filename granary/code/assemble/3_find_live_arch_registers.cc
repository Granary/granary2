/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/cast.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"

#include "granary/code/assemble/fragment.h"
#include "granary/code/assemble/3_find_live_arch_registers.h"

#include "granary/code/register.h"

namespace granary {
namespace {

// Update a register usage set with another fragment. Returns true if we
// expect to find any local changes in our current fragment's register
// liveness set based on the successor having a change in the last data flow
// iteration.
static void UpdateRegUsageFromSuccessor(Fragment *succ,
                                        RegisterUsageTracker *regs) {
  if (succ) {
    regs->Union(succ->entry_regs_live);
  }
}

// Calculate the live registers on entry to a fragment.
static void FindLiveEntryRegsToFrag(Fragment * const frag,
                                    RegisterUsageTracker *regs) {
  for (auto instr : BackwardInstructionIterator(frag->last)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      regs->Visit(ninstr);
    }
  }
}

// Initialize the live entry regs as a data flow problem.
static void InitLiveEntryRegsToFrags(Fragment * const frags) {
  for (auto frag : FragmentIterator(frags)) {
    if (frag->is_exit || frag->is_future_block_head) {
      frag->entry_regs_live.ReviveAll();
    } else {
      frag->entry_regs_live.KillAll();
    }
    frag->exit_regs_live.KillAll();
  }
}

}  // namespace

// Calculate the live registers on entry to every fragment.
void FindLiveEntryRegsToFrags(Fragment * const frags) {
  InitLiveEntryRegsToFrags(frags);

  for (bool data_flow_changed = true; data_flow_changed; ) {
    data_flow_changed = false;
    for (auto frag : FragmentIterator(frags)) {
      if (frag->is_exit || frag->is_future_block_head) {
        continue;
      }

      RegisterUsageTracker regs;
      regs.KillAll();
      UpdateRegUsageFromSuccessor(frag->fall_through_target, &regs);
      UpdateRegUsageFromSuccessor(frag->branch_target, &regs);
      if (regs.Equals(frag->exit_regs_live)) {
        continue;
      }

      frag->exit_regs_live = regs;
      FindLiveEntryRegsToFrag(frag, &regs);
      if (!regs.Equals(frag->entry_regs_live)) {
        frag->entry_regs_live = regs;
        data_flow_changed = true;
      }
    }
  }
}


}  // namespace granary
