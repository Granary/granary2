/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/basic_block.h"

#include "granary/code/fragment.h"

namespace granary {

// Adds in an instruction that forces the end of a fragment, i.e. that control-
// flow cannot pass through. It is reasonable for this to be a debug breakpoint
// instruction or an undefined instruction.
//
// Note: This has an architecture-specific implementation.
extern void AddFragmentEnd(Fragment *frag);

// Adds a fall-through jump, if needed, to this fragment.
//
// Note: This has an architecture-specific implementation.
extern NativeInstruction *AddFallThroughJump(Fragment *frag,
                                             Fragment *fall_through_frag);

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

// Returns true if `instr` branches to in-edge code, or might branch to in-edge
// code.
static bool BranchesToIndirectEdge(NativeInstruction *instr) {
  if (auto cfi = DynamicCast<ControlFlowInstruction *>(instr)) {
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
  auto partition = frag->partition.Value();
  auto branch_target_frag = frag->successors[FRAG_SUCC_BRANCH];
  if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
    if (EDGE_KIND_INVALID != cfrag->edge.kind) {
      GRANARY_ASSERT(nullptr == partition->edge);
      partition->edge = &(cfrag->edge);
    }
    if (cfrag->edge.branches_to_edge_code && cfrag->branch_instr &&
        !cfrag->branch_instr->HasIndirectTarget()) {
      auto branch_target_partition = branch_target_frag->partition.Value();
      GRANARY_ASSERT(partition != branch_target_partition);
      branch_target_partition->edge_patch_instruction = cfrag->branch_instr;
    }
  }

  // Special case: want (specialized) indirect branch targets to be ordered
  // before the fall-through (if any).
  if (partition->edge &&
      EDGE_KIND_INDIRECT == partition->edge->kind &&
      BranchesToIndirectEdge(frag->branch_instr)) {
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

// Adds connectign (direct) control-flow instructions (branches/jumps) between
// fragments, where fall-through is not possible.
void AddConnectingJumps(FragmentList *frags) {
  auto first = frags->First();
  auto next_ptr = &(first->next);
  OrderFragment(first, next_ptr);
  for (auto frag : EncodeOrderedFragmentIterator(first)) {
    auto partition = frag->partition.Value();
    auto fall_through = frag->successors[FRAG_SUCC_FALL_THROUGH];
    auto frag_next = frag->next;

    // We have no fall-through, but some fragment will be encoded after this
    // one in the code cache.
    if (!fall_through) {
      if (frag_next) {
        AddFragmentEnd(frag);
      }
      continue;
    }

    // The fall-through fragment begins some direct edge code. Add a fall-
    // through jump, and this jump will be treated as the code that is patched
    // by Granary to link blocks in the cache.
    auto fall_through_partition = fall_through->partition.Value();
    if (partition != fall_through_partition &&
        fall_through_partition->edge &&
        EDGE_KIND_DIRECT == fall_through_partition->edge->kind) {
      GRANARY_ASSERT(nullptr == fall_through_partition->edge_patch_instruction);
      fall_through_partition->edge_patch_instruction = AddFallThroughJump(
          frag, fall_through);
      continue;
    }

    // Catches cases like `frag_next == nullptr != fall_through`.
    if (fall_through != frag_next) {
      if (partition->edge &&
          EDGE_KIND_INDIRECT == partition->edge->kind &&
          BranchesToIndirectEdge(frag->branch_instr)) {
        continue;
      }
      AddFallThroughJump(frag, fall_through);
    }
  }
}

}  // namespace granary
