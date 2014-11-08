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

struct FragmentWorkList {
  // First fragment on the work list.
  Fragment *next;

  // Pointer to the `Fragment::next` field, so that we can chain fragments
  // together into an encode-ordered list. As fragments are dequeued from the
  // work list, they are appended to the encode-ordered list.
  Fragment **next_ptr;
  int order;

  void Enqueue(Fragment *frag) {
    if (frag && !frag->encoded_order) {
      frag->next = next;
      frag->encoded_order = order++;
      next = frag;
    }
  }
};

// Places fragments into their encoded order. This tries to make sure that
// targets of near jumps are placed directly after the blocks with the branches,
// and it also tries to make sure that specialized call/return/jump lookup
// fragments are executed before anything else.
static void OrderFragment(FragmentWorkList *work_list, Fragment *frag) {
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
    work_list->Enqueue(frag->successors[FRAG_SUCC_FALL_THROUGH]);
    work_list->Enqueue(frag->successors[FRAG_SUCC_BRANCH]);
  } else {
    work_list->Enqueue(frag->successors[FRAG_SUCC_BRANCH]);
    work_list->Enqueue(frag->successors[FRAG_SUCC_FALL_THROUGH]);
  }

  if (swap_successors) {
    std::swap(frag->successors[FRAG_SUCC_BRANCH],
              frag->successors[FRAG_SUCC_FALL_THROUGH]);
  }
}

// Enqueues straggler fragments.
static void EnqueueStragglerFragments(FragmentList *frags,
                                      FragmentWorkList *work_list) {
  for (auto frag : ReverseFragmentListIterator(frags)) {
    if (IsA<NonLocalEntryFragment *>(frag)) {
      work_list->Enqueue(frag);
    }
  }
}

static void OrderFragments(FragmentWorkList *work_list) {
  while (auto curr = work_list->next) {
    work_list->next = curr->next;
    curr->next = nullptr;
    *(work_list->next_ptr) = curr;
    work_list->next_ptr = &(curr->next);

    OrderFragment(work_list, curr);
  }
}

}  // namespace

// Adds connection (direct) control-flow instructions (branches/jumps) between
// fragments, where fall-through is not possible.
void AddConnectingJumps(FragmentList *frags) {
  FragmentWorkList work_list;
  auto first = frags->First();

  work_list.next = nullptr;
  work_list.next_ptr = nullptr;
  work_list.order = 1;

  EnqueueStragglerFragments(frags, &work_list);
  work_list.Enqueue(first);
  work_list.next_ptr = &(first->next);

  OrderFragments(&work_list);

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

      // TODO(pag): Does this handle issues with `NonLocalEntryFragment`s?
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
#ifdef GRANARY_TARGET_debug
  auto trap_frag = new Fragment;
  *(work_list.next_ptr) = trap_frag;
  frags->Append(trap_frag);
  arch::AddFallThroughTrap(trap_frag);
#endif
}

}  // namespace granary
