/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"

#include "granary/code/assemble/fragment.h"
#include "granary/code/assemble/3_partition_fragments.h"

#include "granary/code/metadata.h"

#include "granary/breakpoint.h"
#include "granary/util.h"

namespace granary {
namespace {

// Try to mark some fragment's stack as valid / invalid based on meta-data
// associated with `frag`.
static void AnalyzeFragFromMetadata(Fragment *frag, StackUsageInfo *stack) {
  if (!stack->is_checked) {
    BlockMetaData *block_meta(nullptr);
    if (auto code = DynamicCast<CodeFragment *>(frag)) {
      block_meta = code->attr.block_meta;
    } else if (auto exit_ = DynamicCast<ExitFragment *>(frag)) {
      if (FRAG_EXIT_EXISTING_BLOCK == exit_->kind ||
          FRAG_EXIT_FUTURE_BLOCK == exit_->kind) {
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
    for (auto frag : FragmentIterator(frags)) {
      if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
        changed = PropagateValidity(cfrag) || changed;
      }
    }
  }
  // Mark all remaining unchecked fragments as being on invalid stacks.
  for (auto frag : FragmentIterator(frags)) {
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
//         the stack pointer.
static void GroupFragments(FragmentList *frags) {
  for (auto frag : FragmentIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      GRANARY_ASSERT(nullptr != cfrag->successors[0]);
      for (auto succ : cfrag->successors) {
        if (auto succ_cfrag = DynamicCast<CodeFragment *>(succ)) {
          if (succ_cfrag->attr.block_meta == cfrag->attr.block_meta &&
              succ_cfrag->stack.is_valid == cfrag->stack.is_valid &&
              !cfrag->stack.has_stack_changing_cfi &&
              !succ_cfrag->stack.has_stack_changing_cfi) {
            cfrag->partition.Union(cfrag, succ_cfrag);
          }
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
  auto first = DynamicCast<CodeFragment *>(frags->First());
  AnalyzeFragFromMetadata(first, &(first->stack));
  AnalyzeStackUsage(frags);
  GroupFragments(frags);
}

}  // namespace granary
