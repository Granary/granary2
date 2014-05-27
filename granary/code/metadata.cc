/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/code/metadata.h"

#include "granary/util.h"

namespace granary {

LiveRegisterMetaData::LiveRegisterMetaData(void) {
  live_regs.ReviveAll();
}

// Tells us if we can unify our (uncommitted) meta-data with some existing
// meta-data.
UnificationStatus LiveRegisterMetaData::CanUnifyWith(
    const LiveRegisterMetaData *that) const {

  // Narrow down onto the "best" set of live registers on entry to this basic
  // block. We start with a conservative estimate.
  live_regs.Intersect(that->live_regs);

  return UnificationStatus::ACCEPT;
}

// Update the register meta-data given a block.
bool LiveRegisterMetaData::AnalyzeBlock(DecodedBasicBlock *block) {
  LiveRegisterTracker regs;
  for (auto instr : block->ReversedInstructions()) {
    if (auto cfi = DynamicCast<ControlFlowInstruction *>(instr)) {
      auto target_block = cfi->TargetBlock();

      // Treat all regs as live when doing indirect or native jumps.
      if (IsA<NativeBasicBlock *>(target_block) ||
          IsA<IndirectBasicBlock *>(target_block) ||
          IsA<ReturnBasicBlock *>(target_block)) {
        regs.ReviveAll();
        continue;
      }

      // Bring in register info from existing blocks.
      if (auto inst_block = DynamicCast<InstrumentedBasicBlock *>(block)) {
        auto meta = GetMetaData<LiveRegisterMetaData>(inst_block);
        if (cfi->IsConditionalJump()) {
          regs.Union(meta->live_regs);
        } else {
          regs = meta->live_regs;
        }
      }
    }
    regs.Visit(DynamicCast<NativeInstruction *>(instr));
  }
  auto changed = !live_regs.Equals(regs);
  live_regs = regs;
  return changed;
}

// Tells us if we can unify our (uncommitted) meta-data with some existing
// meta-data.
UnificationStatus StackMetaData::CanUnifyWith(const StackMetaData *that) const {

  // If our block has no information, then just blindly accept the other
  // block. In this case, we don't want to generate excessive numbers of
  // versions of the block.
  //
  // The concern here is this can lead to undefined behavior if, at assembly
  // time, the fragment colorer decides that a successor to the block with
  // this meta-data is using an undefined stack, and this block is using a
  // defined one. In this case, we hope for the best.
  if (!has_stack_hint) {

    // Steal the other information as it's "free" data-flow info :-D
    if (that->has_stack_hint) {
      has_stack_hint = true;
      behaves_like_callstack = that->behaves_like_callstack;
      is_leaf_function = that->is_leaf_function;
    }
    return UnificationStatus::ACCEPT;

  // Be conservative about all else.
  } else if (behaves_like_callstack == that->behaves_like_callstack &&
             is_leaf_function == that->is_leaf_function) {
    return UnificationStatus::ACCEPT;
  } else {
    return UnificationStatus::REJECT;
  }
}

}  // namespace granary
