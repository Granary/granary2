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

// Initialize the live entry regs as a data flow problem.
static void InitFragments(Fragment * const frags) {
  for (auto frag : FragmentIterator(frags)) {
    if (frag->is_exit || frag->is_future_block_head) {
      frag->entry_regs_live.ReviveAll();
    }
  }
}

// Update a register usage set with another fragment. Returns true if we
// expect to find any local changes in our current fragment's register
// liveness set based on the successor having a change in the last data flow
// iteration.
static void JoinFromSuccessor(Fragment *succ, LiveRegisterTracker *live_regs,
                              DeadRegisterTracker *dead_regs) {
  if (succ) {
    live_regs->Join(succ->entry_regs_live);
    dead_regs->Join(succ->entry_regs_dead);
  }
}

// Calculate the live registers on entry to a fragment.
static void VisitInstructions(Fragment * const frag,
                                    LiveRegisterTracker *live_regs,
                                    DeadRegisterTracker *dead_regs) {
  for (auto instr : BackwardInstructionIterator(frag->last)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      live_regs->Visit(ninstr);
      dead_regs->Visit(ninstr);
    }
  }
}

// Calculate the live registers on entry to every fragment.
bool VisitFragment(Fragment * const frag) {
  LiveRegisterTracker live_regs;
  DeadRegisterTracker dead_regs;

  JoinFromSuccessor(frag->fall_through_target, &live_regs, &dead_regs);
  JoinFromSuccessor(frag->branch_target, &live_regs, &dead_regs);

  if (live_regs.Equals(frag->exit_regs_live) &&
      dead_regs.Equals(frag->exit_regs_dead)) {
    return false;
  } else {
    frag->exit_regs_live = live_regs;
    frag->exit_regs_dead = dead_regs;
    VisitInstructions(frag, &live_regs, &dead_regs);
    if (!live_regs.Equals(frag->entry_regs_live) ||
        !dead_regs.Equals(frag->entry_regs_dead)) {
      frag->entry_regs_live = live_regs;
      frag->entry_regs_dead = dead_regs;
      return true;
    }
  }
  return false;
}

}  // namespace

// Calculate the live registers on entry to every fragment.
void FindLiveEntryRegsToFrags(Fragment * const frags) {
  InitFragments(frags);
  for (bool data_flow_changed = true; data_flow_changed; ) {
    data_flow_changed = false;
    for (auto frag : FragmentIterator(frags)) {
      if (frag->is_exit || frag->is_future_block_head) {
        continue;
      }
      data_flow_changed = VisitFragment(frag) || data_flow_changed;
    }
  }
}


}  // namespace granary
