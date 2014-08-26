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
      // Otherwise `has_native_instrs` would be `true` for `frag`.
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
      if (cfrag->attr.is_return_target) break;
      if (!cfrag->attr.can_add_succ_to_partition) break;
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

// Set the stack validity from some meta-data.
static void InheritMetaDataStackValidity(StackUsageInfo *stack,
                                         BlockMetaData *meta) {
  auto stack_meta = MetaDataCast<StackMetaData *>(meta);
  if (stack_meta->has_stack_hint && stack_meta->behaves_like_callstack) {
    stack->status = STACK_VALID;
  }
}

// Initializes the stack validity analysis.
static void InitStackValidity(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    auto cfrag = DynamicCast<CodeFragment *>(frag);
    if (!cfrag) continue;

    auto stack = &(cfrag->stack);
    if (STACK_UNKNOWN != stack->status) continue;

    for (auto succ : cfrag->successors) {
      auto exit_succ = DynamicCast<ExitFragment *>(succ);
      if (!exit_succ) continue;

      // In kernel space, all exits are seen as going to a valid stack.
      if (GRANARY_IF_KERNEL_ELSE(!arch::REDZONE_SIZE_BYTES, false)) {
        stack->status = STACK_VALID;

      // Try to get the validity based on the successor block's stack
      // validity as recorded in its meta-data.
      } else if (FRAG_EXIT_EXISTING_BLOCK == exit_succ->kind ||
                 FRAG_EXIT_FUTURE_BLOCK_DIRECT == exit_succ->kind ||
                 FRAG_EXIT_FUTURE_BLOCK_INDIRECT == exit_succ->kind) {
        InheritMetaDataStackValidity(stack, exit_succ->block_meta);
      }
    }
  }
}

// Back propagate stack validity from successors to predecessors.
static bool BackPropagateValidity(FragmentList *frags) {
  auto made_progess = false;
  for (auto frag : ReverseFragmentListIterator(frags)) {
    auto cfrag = DynamicCast<CodeFragment *>(frag);
    if (!cfrag) continue;

    auto stack = &(cfrag->stack);

    if (STACK_UNKNOWN != stack->status) continue;
    if (!(STACK_STATUS_INHERIT_SUCC & stack->inherit_constraint)) continue;

    for (auto succ : cfrag->successors) {
      if (auto code_succ = DynamicCast<CodeFragment *>(succ)) {
        if (STACK_VALID == code_succ->stack.status) {
          stack->status = STACK_VALID;  // Might lead to forward propagation.
          made_progess = true;
          break;
        }
      }
    }
  }
  return made_progess;
}

// Forward propagate stack validity from predecessors to successors.
static bool ForwardPropagateValidity(FragmentList *frags) {
  auto made_progess = false;
  for (auto frag : FragmentListIterator(frags)) {
    auto cfrag = DynamicCast<CodeFragment *>(frag);
    if (!cfrag) continue;

    auto stack = &(cfrag->stack);
    if (STACK_VALID != stack->status) continue;

    for (auto succ : frag->successors) {
      if (auto code_succ = DynamicCast<CodeFragment *>(succ)) {
        auto succ_stack = &(code_succ->stack);
        if (STACK_UNKNOWN != succ_stack->status) continue;
        if (0 != (STACK_STATUS_INHERIT_PRED & succ_stack->inherit_constraint)) {
          succ_stack->status = STACK_VALID;
          made_progess = true;
        }
      }
    }
  }
  return made_progess;
}

// Analyze the stack usage of fragments to determine which fragments operate on
// a valid thread stack, and which fragments cannot be proved to operate on a
// valid thread stack.
//
// This analysis depends on `2_build_fragment_list.cc` marking some fragments
// ahead of time as being valid/invalid based on information passed to it via
// the early mangler and stack definedness annotation instructions.
static void AnalyzeStackUsage(FragmentList * const frags) {
  InitStackValidity(frags);
  auto first_frag = DynamicCast<CodeFragment *>(frags->First());
  for (auto changed = true; changed; ) {
    changed = BackPropagateValidity(frags);

    for (;;) {
      changed = ForwardPropagateValidity(frags) || changed;

      // If we haven't made progress, try to get the first fragment's validity
      // from its meta-data.
      if (!changed && first_frag && STACK_UNKNOWN == first_frag->stack.status) {
        InheritMetaDataStackValidity(&(first_frag->stack),
                                     first_frag->attr.block_meta);
        first_frag = nullptr;
      } else {
        break;
      }
    }
  }

  // Mark all remaining unchecked fragments as being on invalid stacks.
  for (auto frag : FragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      if (STACK_UNKNOWN == cfrag->stack.status || true) {
        cfrag->stack.status = STACK_INVALID;
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

      // Successors of this fragment can't be added to the same partition.
      if (!cfrag->attr.can_add_succ_to_partition) continue;

      for (auto succ : cfrag->successors) {
        if (auto succ_cfrag = DynamicCast<CodeFragment *>(succ)) {
          if (frag->partition == succ->partition) continue;

          // Note: There is one case where the above condition is true, but this
          //       is false: indirect edge code, that has a meta-data template.
          //       In this case, the above condition is forced to be true by
          //       the code that generates the indirect edge code fragments.
          if (succ_cfrag->attr.block_meta != cfrag->attr.block_meta) continue;

          if (succ_cfrag->stack.status != cfrag->stack.status) continue;
          cfrag->partition.Union(cfrag, succ_cfrag);
        }
      }
    }
  }
}

// Try to propagate stack validity to future blocks.
static void PropagateValidityToExitFragments(CodeFragment *frag) {
  for (auto succ : frag->successors) {
    auto exit_succ = DynamicCast<ExitFragment *>(succ);

    if (!exit_succ) continue;
    if (EDGE_KIND_INVALID == exit_succ->edge.kind) continue;

    auto stack_meta = MetaDataCast<StackMetaData *>(exit_succ->block_meta);
    if (stack_meta->has_stack_hint) continue;

    if (STACK_VALID == frag->stack.status) {
      stack_meta->MarkStackAsValid();
    } else {
      stack_meta->MarkStackAsInvalid();
    }
  }
}

// Updates the block meta-data with the stack tracking info.
static void UpdateMetaData(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      if (cfrag->attr.is_block_head) {
        auto stack_meta = MetaDataCast<StackMetaData *>(cfrag->attr.block_meta);
        if (STACK_VALID == cfrag->stack.status) {
          stack_meta->MarkStackAsValid();
        } else {
          stack_meta->MarkStackAsInvalid();
        }
      }
      PropagateValidityToExitFragments(cfrag);
    }
  }
}

}  // namespace

// Partition the fragments into groups, where two fragments belong to the same
// group (partition) iff they are connected by control flow, if they belong to
// the same basic block, and if the stack pointer does not change between them.
void PartitionFragments(FragmentList *frags) {
  RemoveUselessFrags(frags);
  AnalyzeStackUsage(frags);
  GroupFragments(frags);
  UpdateMetaData(frags);
}

}  // namespace granary
