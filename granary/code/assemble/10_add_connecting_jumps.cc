/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/basic_block.h"

#include "granary/code/fragment.h"

#include "granary/code/assemble/10_add_connecting_jumps.h"

namespace granary {
namespace arch {

// Adds a fall-through jump, if needed, to this fragment.
//
// Note: This has an architecture-specific implementation.
extern NativeInstruction *AddFallThroughJump(Fragment *frag,
                                             Fragment *fall_through_frag);

// Returns true if the target of a jump must be encoded in a nearby location.
//
// Note: This has an architecture-specific implementation.
extern bool IsNearRelativeJump(NativeInstruction *instr);

}  // namespace arch
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

// Add the fragments to a total ordering.
static Fragment **OrderFragment(Fragment *frag, Fragment **next_ptr) {
  // Special case: want (specialized) indirect branch targets to be ordered
  // before the fall-through (if any). This affects the determination on
  // whether or not a fall-through branch needs to be added.
  auto swap_successors = false;
  auto visit_branch_first = false;
  if (auto cfi = DynamicCast<ControlFlowInstruction *>(frag->branch_instr)) {
    auto target_block = cfi->TargetBlock();
    swap_successors = IsA<IndirectBasicBlock *>(target_block) ||
                      IsA<ReturnBasicBlock *>(target_block);
    visit_branch_first = swap_successors || arch::IsNearRelativeJump(cfi);
  } else if (auto br = DynamicCast<BranchInstruction *>(frag->branch_instr)) {
    visit_branch_first = arch::IsNearRelativeJump(br);
  }

  if (visit_branch_first) {
    next_ptr = VisitOrderedFragment(frag->successors[FRAG_SUCC_BRANCH],
                                    next_ptr);
  }

  // Default: depth-first order, where fall-through naturally comes up as a
  // straight-line preference.
  for (auto succ : frag->successors) {
    next_ptr = VisitOrderedFragment(succ, next_ptr);
  }

  if (swap_successors) {
    std::swap(frag->successors[FRAG_SUCC_BRANCH],
              frag->successors[FRAG_SUCC_FALL_THROUGH]);
  }

  return next_ptr;
}

}  // namespace

// Adds connection (direct) control-flow instructions (branches/jumps) between
// fragments, where fall-through is not possible.
void AddConnectingJumps(FragmentList *frags) {
  auto first = frags->First();
  auto next_ptr = &(first->next);
  first->was_encode_ordered = true;
  OrderFragment(first, next_ptr);
  for (auto frag : EncodeOrderedFragmentIterator(first)) {
    auto fall_through = frag->successors[FRAG_SUCC_FALL_THROUGH];
    auto frag_next = frag->next;

    // No fall-through.
    if (!fall_through) {
      continue;

    // Last fragment in the list, but it has a fall-through.
    } else if (!frag_next) {
      arch::AddFallThroughJump(frag, fall_through);

    // Has a fall-through that's not the next fragment.
    } else if (fall_through != frag_next) {
      arch::AddFallThroughJump(frag, fall_through);
    }
  }
}

}  // namespace granary
