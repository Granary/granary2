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

// Find dead registers on exit of a fragment. This information is used in a
// later stage, when we're looking at definitions of virtual registers based on
// physical registers, to see if the physical registers have changed before they
// are used.
static void FindDeadExitRegsFromFrag(Fragment * const frags) {
  for (auto frag : FragmentIterator(frags)) {
    frag->exit_regs_dead.ReviveAll();
    if (frag->fall_through_target) {
      frag->exit_regs_dead.Intersect(
          frag->fall_through_target->entry_regs_live);
    }
    if (frag->branch_target) {
      frag->exit_regs_dead.Intersect(frag->branch_target->entry_regs_live);
    }
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
  FindDeadExitRegsFromFrag(frags);
}


}  // namespace granary
