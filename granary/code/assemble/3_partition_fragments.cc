/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"

#include "granary/code/fragment.h"
#include "granary/code/assemble/3_partition_fragments.h"

#include "granary/code/metadata.h"

#include "granary/breakpoint.h"
#include "granary/util.h"

namespace granary {
namespace {


// Returns true if this fragment has some useful instructions. Here we really
// mean some labels that are targeted by at least one other fragment.
static bool HasUsefulInstructions(CodeFragment *frag) {
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto annot = DynamicCast<AnnotationInstruction *>(instr)) {
      // Labels that are targeted by at least one branch.
      // Return addresses.
      if (annot->data) return true;
    } else {
      // Elsewise `has_native_instrs` would be `true` for `frag`.
      GRANARY_ASSERT(!IsA<NativeInstruction *>(instr));
    }
  }
  return false;
}

// Removes a fragment that has been identified as being useless.
static Fragment *UnlinkUselessFrag(FragmentList *frags, Fragment *prev,
                                   CodeFragment *frag,
                                   Fragment **removed_list) {
  frags->Remove(frag);
  frag->next = *removed_list;
  *removed_list = frag;
  return prev;
}

// Returns `true` if `frag` is linked in to a larger list of fragments.
static bool IsLinked(Fragment *frag) {
  return frag->list.GetNext(frag) || frag->list.GetPrevious(frag);
}

// Assuming that `frag` is not linked to a fragment list, this function returns
// a pointer to the next linked fragment that is reachable by following one or
// more fall-through branches.
//
// TODO(pag): In some unusual circumstances this could actually be an infinite
//            loop. Most likely it would occur if instrumentation injected an
//            empty infinite loop.
static Fragment *NextLinkedFallThrough(Fragment *frag) {
  do {
    if (auto fall_through = frag->successors[FRAG_SUCC_FALL_THROUGH]) {
      frag = fall_through;
    } else {
      frag = frag->successors[FRAG_SUCC_BRANCH];
    }
  } while (!IsLinked(frag));
  return frag;
}

// Free the instructions from a fragment.
static void FreeInstructions(Fragment *frag) {
  auto instr = frag->instrs.First();
  for (Instruction *next_instr(nullptr); instr; instr = next_instr) {
    next_instr = instr->Next();
    instr->UnsafeUnlink();  // Will self-destruct.
  }
}

// Removes "useless" fragments so that we don't clutter the fragment list with
// an excessive number of partition/flag entry/exit fragments that surround an
// otherwise empty fragment.
static void RemoveUselessFrags(FragmentList *frags) {
  auto prev = frags->First();
  auto curr = prev->list.GetNext(prev);
  Fragment *removed_list(nullptr);

  // Find the fragments that we want to remove, and unlink them from the
  // fragment list.
  for (; curr; ) {
    while (auto cfrag = DynamicCast<CodeFragment *>(curr)) {
      if (cfrag->attr.has_native_instrs) break;
      if (cfrag->attr.is_block_head) break;
      if (cfrag->branch_instr) break;
      if (cfrag->successors[FRAG_SUCC_BRANCH]) break;
      if (HasUsefulInstructions(cfrag)) break;
      curr = UnlinkUselessFrag(frags, prev, cfrag, &removed_list);
      break;
    }
    prev = curr;
    curr = prev->list.GetNext(prev);
  }

  if (!removed_list) return;

  // Unlink the fragments that we want to remove from the control-flow graph.
  for (auto frag : FragmentListIterator(frags)) {
    for (auto &succ : frag->successors) {
      if (!IsA<CodeFragment *>(succ) || IsLinked(succ)) continue;
      succ = NextLinkedFallThrough(succ);
    }
  }

  // Remove the fragments in the `remove_list`.
  do {
    auto next = removed_list->next;
    FreeInstructions(removed_list);
    delete removed_list;
    removed_list = next;
  } while (removed_list);
}

// Try to mark some fragment's stack as valid / invalid based on meta-data
// associated with `frag`.
static void AnalyzeFragFromMetadata(Fragment *frag, StackUsageInfo *stack) {
  if (!stack->is_checked) {
    BlockMetaData *block_meta(nullptr);
    if (auto code = DynamicCast<CodeFragment *>(frag)) {
      block_meta = code->attr.block_meta;
    } else if (auto exit_ = DynamicCast<ExitFragment *>(frag)) {
      if (FRAG_EXIT_EXISTING_BLOCK == exit_->kind ||
          FRAG_EXIT_FUTURE_BLOCK_DIRECT == exit_->kind ||
          FRAG_EXIT_FUTURE_BLOCK_INDIRECT == exit_->kind) {
        block_meta = exit_->block_meta;
      }
    }
    if (auto stack_meta = MetaDataCast<StackMetaData *>(block_meta)) {
      if (stack_meta->has_stack_hint) {
        stack->is_checked = true;
        stack->is_valid = stack_meta->behaves_like_callstack;
      }
    }
  }
}

// Analyzes the stack validity of an individual fragment.
static bool PropagateValidity(CodeFragment * const frag) {
  auto updated = false;
  if (!frag->stack.is_checked) {  // Back-propagate.
    for (auto succ : frag->successors) {
      if (auto code = DynamicCast<CodeFragment *>(succ)) {
        if (code->stack.is_valid) {  // Might lead to forward propagation.
          frag->stack.is_checked = true;
          frag->stack.is_valid = code->stack.is_valid;
          updated = true;
          break;
        }
      } else {
        AnalyzeFragFromMetadata(succ, &(frag->stack));
        updated = frag->stack.is_checked;
      }
    }
  }
  if (frag->stack.is_valid) {  // Forward-propagate.
    for (auto succ : frag->successors) {
      if (auto code = DynamicCast<CodeFragment *>(succ)) {
        if (!code->stack.is_checked) {
          code->stack.is_checked = true;
          code->stack.is_valid = frag->stack.is_valid;
          updated = true;
        }
      }
    }
  }
  return updated;
}

// Analyze the stack usage of fragments to determine which fragments operate on
// a valid thread stack, and which fragments cannot be proved to operate on a
// valid thread stack.
//
// This analysis depends on `2_build_fragment_list.cc` marking some fragments
// ahead of time as being valid/invalid based on information passed to it via
// the early mangler and stack definedness annotation instructions.
static void AnalyzeStackUsage(FragmentList * const frags) {
  for (auto changed = true; changed; ) {
    changed = false;
    for (auto frag : FragmentListIterator(frags)) {
      if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
        changed = PropagateValidity(cfrag) || changed;
      }
    }
  }
  // Mark all remaining unchecked fragments as being on invalid stacks.
  for (auto frag : FragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      if (!cfrag->stack.is_checked) {
        cfrag->stack.is_checked = true;
        cfrag->stack.is_valid = false;
      }
    }
  }
}

// Group fragments. Two fragments can be grouped if:
//      1) The fragments originate from the same decoded basic block.
//      2) The stack validity between the two fragments is the same.
//      3) Neither fragment contains a control-flow instruction that changes
//         the stack pointer. This condition is not strictly tested here, and
//         does not apply in all cases due to allowances for edge code.
static void GroupFragments(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      if (!cfrag->attr.can_add_to_partition) continue;
      for (auto succ : cfrag->successors) {
        if (auto succ_cfrag = DynamicCast<CodeFragment *>(succ)) {
          if (succ_cfrag->attr.block_meta != cfrag->attr.block_meta) continue;
          if (!succ_cfrag->attr.can_add_to_partition) continue;
          if (succ_cfrag->stack.is_valid != cfrag->stack.is_valid) continue;
          cfrag->partition.Union(cfrag, succ_cfrag);
        }
      }
    }
  }
}

}  // namespace

// Partition the fragments into groups, where two fragments belong to the same
// group (partition) iff they are connected by control flow, if they belong to
// the same basic block, and if the stack pointer does not change between them.
void PartitionFragments(FragmentList *frags) {
  RemoveUselessFrags(frags);
  auto first = DynamicCast<CodeFragment *>(frags->First());
  AnalyzeFragFromMetadata(first, &(first->stack));
  AnalyzeStackUsage(frags);
  GroupFragments(frags);
}

}  // namespace granary
