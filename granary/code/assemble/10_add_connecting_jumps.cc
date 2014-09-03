/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/basic_block.h"

#include "granary/code/fragment.h"

#include "granary/code/assemble/10_add_connecting_jumps.h"

namespace granary {
namespace arch {

// Don't encode `instr`, but leave it in place.
//
// Note: This has an architecture-specific implementation.
extern void ElideInstruction(Instruction *instr);

// Adds a fall-through jump, if needed, to this fragment.
//
// Note: This has an architecture-specific implementation.
extern NativeInstruction *AddFallThroughJump(Fragment *frag,
                                             Fragment *fall_through_frag);

// Returns true if the target of a jump must be encoded in a nearby location.
//
// Note: This has an architecture-specific implementation.
extern bool IsNearRelativeJump(NativeInstruction *instr);

// Catches erroneous fall-throughs off the end of the basic block.
GRANARY_IF_DEBUG( extern void AddFallThroughTrap(Fragment *frag); )

}  // namespace arch
namespace {

static Fragment **OrderFragment(Fragment *frag, Fragment **next_ptr,
                                int * const order);

static Fragment **VisitOrderedFragment(Fragment *succ, Fragment **next_ptr,
                                       int * const order) {
  if (succ && !succ->encoded_order) {
    *next_ptr = succ;
    next_ptr = &(succ->next);
    succ->encoded_order = (*order)++;
    return OrderFragment(succ, next_ptr, order);
  } else {
    return next_ptr;
  }
}

// Add the fragments to a total ordering.
//
// TODO(pag): Turn this recursion into a work-list based approach, ideally by
//            using `Fragment::next`.
static Fragment **OrderFragment(Fragment *frag, Fragment **next_ptr,
                                int * const order) {
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
                                    next_ptr, order);
  }

  // Default: depth-first order, where fall-through naturally comes up as a
  // straight-line preference.
  for (auto succ : frag->successors) {
    next_ptr = VisitOrderedFragment(succ, next_ptr, order);
  }

  if (swap_successors) {
    std::swap(frag->successors[FRAG_SUCC_BRANCH],
              frag->successors[FRAG_SUCC_FALL_THROUGH]);
  }

  return next_ptr;
}

// Try to remove useless direct jump instructions that will only have a zero
// displacement.
static void TryElideBranches(NativeInstruction *branch_instr) {
  auto &ainstr(branch_instr->instruction);

  // Note: Using the `ainstr` instead of `branch_instr` for checks as some
  //       direct jumps to native code are mangled into indirect jumps (because
  //       the target is too far away), but this is hidden, except from the
  //       `arch::Instruction` interface.
  if (ainstr.IsJump() && !ainstr.IsConditionalJump() &&
      !ainstr.HasIndirectTarget() &&
      (IsA<BranchInstruction *>(branch_instr) ||
       IsA<ControlFlowInstruction *>(branch_instr))) {
    arch::ElideInstruction(&ainstr);
  }
}

}  // namespace

// Adds connection (direct) control-flow instructions (branches/jumps) between
// fragments, where fall-through is not possible.
void AddConnectingJumps(FragmentList *frags) {
  auto first = frags->First();
  auto next_ptr = &(first->next);
  int order(2);
  first->encoded_order = 1;
  GRANARY_IF_DEBUG( next_ptr = ) OrderFragment(first, next_ptr, &order);

  for (auto frag : EncodeOrderedFragmentIterator(first)) {
    auto frag_fall_through = frag->successors[FRAG_SUCC_FALL_THROUGH];
    auto frag_branch = frag->successors[FRAG_SUCC_BRANCH];
    auto frag_next = frag->next;

    if (frag_branch && frag_branch == frag_next && !frag_branch->encoded_pc) {
      TryElideBranches(frag->branch_instr);
    }

    // No fall-through.
    if (!frag_fall_through) {

      // Not sure if this can happen: we've got a direct jump that behaves like
      // a fall-through, but the next fragment isn't the jump's target, and the
      // jump itself won't be encoded.
      GRANARY_ASSERT(!(frag->branch_instr && frag_next != frag_branch &&
                       !frag->branch_instr->instruction.WillBeEncoded()));
      continue;

    // Last fragment in the list, but it has a fall-through.
    } else if (!frag_next) {
      arch::AddFallThroughJump(frag, frag_fall_through);

    // Has a fall-through that's not the next fragment.
    } else if (frag_fall_through != frag_next) {
      arch::AddFallThroughJump(frag, frag_fall_through);

    // Has a fall-through that's an exit fragment.
    } else if (IsA<ExitFragment *>(frag_fall_through) &&
               frag_fall_through->encoded_pc) {
      arch::AddFallThroughJump(frag, frag_fall_through);
    }
  }

  // Helps to debug the case where execution falls off the end of a basic block.
  GRANARY_IF_DEBUG( *next_ptr = new Fragment; );
  GRANARY_IF_DEBUG( frags->Append(*next_ptr); )
  GRANARY_IF_DEBUG( arch::AddFallThroughTrap(*next_ptr); )
}

}  // namespace granary
