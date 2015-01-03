/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/block.h"

#include "granary/code/fragment.h"

#include "granary/code/assemble/10_add_connecting_jumps.h"

namespace granary {
namespace arch {

// Adds a fall-through jump, if needed, to this fragment.
//
// Note: This has an architecture-specific implementation.
extern void AddFallThroughJump(Fragment *frag, Fragment *fall_through_frag);

// Returns true if the target of a jump must be encoded in a nearby location.
//
// Note: This has an architecture-specific implementation.
extern bool IsNearRelativeJump(NativeInstruction *instr);

// Try to negate the branch condition. Returns `false` if the branch condition
// was not merged.
//
// Note: This has an architecture-specific implementation.
extern bool TryNegateBranchCondition(NativeInstruction *instr);

// Catches erroneous fall-throughs off the end of the basic block.
GRANARY_IF_DEBUG( extern void AddFallThroughTrap(Fragment *frag); )

}  // namespace arch
namespace {

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
  auto frag_fall_through = frag->successors[kFragSuccFallThrough];
  auto frag_branch = frag->successors[kFragSuccBranch];

  if (!frag_branch && !frag_fall_through) return;

  if (frag_branch && !frag_fall_through) {
    work_list->Enqueue(frag_branch);

  } else if (frag_fall_through && !frag_branch) {
    work_list->Enqueue(frag_fall_through);

  // Branch is hotter than fall-through.
  } else if (frag_branch->cache < frag_fall_through->cache) {

    // Branch keeps us in our tier, but fall-through goes to a colder tier.
    // Lets swap the two and negate the branch.
    if (frag->cache == frag_branch->cache &&
        arch::TryNegateBranchCondition(frag->branch_instr)) {
      frag->successors[kFragSuccFallThrough] = frag_branch;
      frag->successors[kFragSuccBranch] = frag_fall_through;
    }

    work_list->Enqueue(frag_fall_through);
    work_list->Enqueue(frag_branch);

  // Fall-through is hotter than branch.
  } else if (frag_fall_through->cache < frag_branch->cache) {
    work_list->Enqueue(frag_branch);
    work_list->Enqueue(frag_fall_through);

  // Same hotness, branch uses short jump.
  } else if (arch::IsNearRelativeJump(frag->branch_instr)) {
    work_list->Enqueue(frag_fall_through);
    work_list->Enqueue(frag_branch);

  } else {
    work_list->Enqueue(frag_branch);
    work_list->Enqueue(frag_fall_through);
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
  auto encoded_order = 1;
  while (auto curr = work_list->next) {
    work_list->next = curr->next;
    curr->next = nullptr;
    curr->encoded_order = encoded_order++;
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
    auto frag_next = frag->next;
    auto frag_fall_through = frag->successors[kFragSuccFallThrough];
    auto frag_branch = frag->successors[kFragSuccBranch];

    if (!frag_fall_through) {

      // TODO(pag): Does it matter if it's conditional?
      if (frag_branch && frag_next == frag_branch && frag->branch_instr &&
          frag->cache == frag_branch->cache &&
          !frag_branch->encoded_pc) {
        frag->branch_instr->instruction.DontEncode();
      }

    } else if (frag_fall_through != frag_next ||
               frag_fall_through->encoded_pc ||
               frag->cache != frag_fall_through->cache) {
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
