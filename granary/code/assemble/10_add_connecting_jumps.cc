/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/code/fragment.h"

namespace granary {

// Adds a fall-through jump, if needed, to this fragment.
//
// Note: This has an architecture-specific implementation.
extern void AddFallThroughJump(Fragment *frag);

namespace {

// Returns `true` if this fragment has at least one instruction that actually
// does something.
static bool HasUsefulInstruction(const Fragment *frag) {
  for (auto instr : ReverseInstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if (!ninstr->instruction.IsNoOp()) return true;
    }
  }
  return false;
}

// Add the fragments to a total ordering.
static void OrderFragment(Fragment *frag, Fragment **next_ptr) {
  for (auto succ : frag->successors) {
    if (succ && !succ->was_encode_ordered) {
      *next_ptr = succ;
      next_ptr = &(succ->next);
      succ->was_encode_ordered = true;
      OrderFragment(succ, next_ptr);
    }
  }
}

}  // namespace

// Adds connectign (direct) control-flow instructions (branches/jumps) between
// fragments, where fall-through is not possible.
void AddConnectingJumps(FragmentList *frags) {
  auto first = frags->First();
  auto next_ptr = &(first->next);
  OrderFragment(first, next_ptr);
  for (auto frag : EncodeOrderedFragmentIterator(first)) {
    auto fall_through = frag->successors[FRAG_SUCC_FALL_THROUGH];
    if (fall_through && HasUsefulInstruction(frag)) {
      auto frag_next = frag->next;
      if (fall_through != frag_next) {
        AddFallThroughJump(frag);
      }
    }
  }
}

}  // namespace granary
