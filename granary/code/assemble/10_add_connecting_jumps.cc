/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/basic_block.h"

#include "granary/code/fragment.h"

namespace granary {

// Adds a fall-through jump, if needed, to this fragment.
//
// Note: This has an architecture-specific implementation.
extern void AddFallThroughJump(Fragment *frag);

namespace {

static Fragment **OrderFragment(Fragment *frag, Fragment **next_ptr);

static Fragment **VisitOrderedFragment(Fragment *succ, Fragment **next_ptr) {
  if (succ && !succ->was_encode_ordered) {
    *next_ptr = succ;
    next_ptr = &(succ->next);
    succ->was_encode_ordered = true;
    return OrderFragment(succ, next_ptr);
  } else {
    return next_ptr;
  }
}

// Returns true if the `branch_instr` branches to in-edge code, or might branch
// to in-edge code.
static bool HasInEdgeCode(Fragment *frag) {
  if (auto cfi = DynamicCast<ControlFlowInstruction *>(frag->branch_instr)) {
    auto target_block = cfi->TargetBlock();
    if (IsA<IndirectBasicBlock *>(target_block) ||
        IsA<ReturnBasicBlock *>(target_block)) {
      return !cfi->IsSystemCall() && !cfi->IsInterruptCall();
    }
  }
  return false;
}

// Add the fragments to a total ordering.
static Fragment **OrderFragment(Fragment *frag, Fragment **next_ptr) {
  // Special case: want (specialized) indirect branch targets to be ordered
  // before the fall-through (if any).
  if (frag->branch_instr && HasInEdgeCode(frag)) {
    next_ptr = VisitOrderedFragment(frag->successors[FRAG_SUCC_BRANCH],
                                    next_ptr);
  }

  // Default: depth-first order, where fall-through naturally comes up as a
  // straight-line preference.
  for (auto succ : frag->successors) {
    next_ptr = VisitOrderedFragment(succ, next_ptr);
  }

  return next_ptr;
}

}  // namespace

// Adds connectign (direct) control-flow instructions (branches/jumps) between
// fragments, where fall-through is not possible.
void AddConnectingJumps(FragmentList *frags) {
  auto first = frags->First();
  auto next_ptr = &(first->next);
  OrderFragment(first, next_ptr);
  for (auto frag : EncodeOrderedFragmentIterator(first)) {
    if (frag->branch_instr && HasInEdgeCode(frag)) {
      continue;  // Don't add a fall-through; handled via other means.
    }
    if (auto fall_through = frag->successors[FRAG_SUCC_FALL_THROUGH]) {
      auto frag_next = frag->next;
      if (fall_through != frag_next) {
        AddFallThroughJump(frag);
      }
    }
  }
}

}  // namespace granary
