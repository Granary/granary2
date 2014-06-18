/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/basic_block.h"

#include "granary/code/fragment.h"

namespace granary {

// Adds a fall-through jump, if needed, to this fragment.
//
// Note: This has an architecture-specific implementation.
extern NativeInstruction *AddFallThroughJump(Fragment *frag,
                                             Fragment *fall_through_frag);

// Adds in an instruction that forces the end of a fragment, i.e. that control-
// flow cannot pass through. It is reasonable for this to be a debug breakpoint
// instruction or an undefined instruction.
//
// Note: This has an architecture-specific implementation.
void AddFragmentEnd(Fragment *frag);

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
  // before the fall-through (if any).
  auto branch_target_frag = frag->successors[FRAG_SUCC_BRANCH];
  if (frag->branch_instr && frag->branch_instr->HasIndirectTarget()) {
    next_ptr = VisitOrderedFragment(branch_target_frag, next_ptr);
  }

  // Default: depth-first order, where fall-through naturally comes up as a
  // straight-line preference.
  for (auto succ : frag->successors) {
    next_ptr = VisitOrderedFragment(succ, next_ptr);
  }

  return next_ptr;
}

}  // namespace

// Adds connection (direct) control-flow instructions (branches/jumps) between
// fragments, where fall-through is not possible.
void AddConnectingJumps(FragmentList *frags) {
  auto first = frags->First();
  auto next_ptr = &(first->next);
  Fragment *last_frag = nullptr;
  OrderFragment(first, next_ptr);
  for (auto frag : EncodeOrderedFragmentIterator(first)) {
    auto fall_through = frag->successors[FRAG_SUCC_FALL_THROUGH];
    auto frag_next = frag->next;

    if (!IsA<ExitFragment *>(frag)) {
      last_frag = frag;
    }

    // No fall-through.
    if (!fall_through) {
      continue;

    // Last fragment in the list, but it has a fall-through.
    } else if (!frag_next) {
      AddFallThroughJump(frag, fall_through);

    // Has a fall-through that's not the next fragment.
    } else if (fall_through != frag_next) {
      AddFallThroughJump(frag, fall_through);
    }
  }

  // Architecture-specific, but can be used to add an instruction that will
  // prevent prefetching beyond the last instruction of what we're encoding.
  // This can make the difference between self/cross-modifying code (modifying
  // existing instructions), and dynamic code generation (adding new code
  // somewhere where execution has never reached).
  AddFragmentEnd(last_frag);
}

}  // namespace granary
