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

#if 0

// Implements the necessary forward and backward data-flow passes to partition/
// color fragments such that two fragments are colored the same if and only if:
//    1) The fragments belong to the same (decoded) basic block.
//    2) The fragments are connected by direct control-flow.
//    3) For some pair `(pred, succ)` of fragments, the stack pointer does not
//       change in `pred`, and at most changes only in the last instruction
//       of `succ`.
class FragmentColorer {
 public:
  // Intialize the fragment colorer.
  explicit FragmentColorer(Fragment * const frags_)
      : next_invalid_id(-1),
        next_valid_id(1),
        frags(frags_) {}

  // If this fragment is cached then check its meta-data. Mostly we actually
  // care not about this fragment, but about fragments targeting this
  // fragment.
  //
  // We check against the first fragment because we don't want to penalize
  // the first fragment into a different color if back propagation can give
  // it a color on its own.
  bool ColorFragmentByMetaData(CodeFragment *frag, Fragment *first_frag) {
    auto stack_meta = MetaDataCast<StackMetaData *>(frag->block_meta);
    if (frag != first_frag && stack_meta->has_stack_hint) {
      if (stack_meta->behaves_like_callstack) {
        MarkAsValid(frag);
      } else {
        MarkAsInvalid(frag);
      }
      return true;
    }
    return false;
  }

  // Initialize the fragment coloring.
  void Initialize(void) {
    for (auto frag_ : FragmentIterator(frags)) {
      if (auto frag = DynamicCast<CodeFragment *>(frag_)) {
        ColorFragmentByCFI(frag);
        if (!frag->partition->id) {
          if (frag->reads_from_stack_pointer) {
            MarkAsValid(frag);  // Reads & writes the stack pointer.
          } else if (frag->block_meta && frag->is_exit) {
            ColorFragmentByMetaData(frag, frags);
          }
        }
      } else if (auto frag = DynamicCast<ExitFragment *>(frag_)) {

      }
    }
  }

  // Finalize the fragment coloring.
  void Finalize(void) {
    for (auto frag : FragmentIterator(frags)) {
      if (!frag->partition->id) {
        MarkAsInvalid(frag);
      }
    }
  }

  // Perform a backward data-flow pass on the fragment stack ID colorings.
  bool BackPropagate(void) {
    auto global_changed = false;
    for (auto changed = true; changed; ) {
      changed = false;
      for (auto frag_ : FragmentIterator(frags)) {
        if (auto frag = DynamicCast<CodeFragment *>(frag_)) {
          for (auto succ : frag->successors) {
            if (succ && !frag->partition->id &&
                !frag->writes_to_stack_pointer &&
                succ->partition->id) {
              changed = PropagateColor(succ, frag) || changed;
            }
          }
        }
      }
      global_changed = global_changed || changed;
    }
    return global_changed;
  }

  // Perform a forward data-flow pass on the fragment stack ID colorings.
  bool ForwardPropagate(void) {
    auto global_changed = false;
    for (auto changed = true; changed; ) {
      changed = false;
      for (auto frag_ : FragmentIterator(frags)) {
        auto frag = DynamicCast<CodeFragment *>(frag_);
        if (!frag->partition->id || frag->writes_to_stack_pointer) {
          continue;
        }
        changed = PropagateColor(frag, frag->successors[0]) || changed;
        changed = PropagateColor(frag, frag->successors[1]) || changed;
      }
      global_changed = global_changed || changed;
    }
    return global_changed;
  }

 private:
  // Mark a fragment as having a stack pointer that appears to behave like
  // a C-style call stack.
  void MarkAsValid(Fragment *frag) {
    if (frag) {
      GRANARY_ASSERT(0 <= frag->partition->id);
      if (!frag->partition->id) {
        frag->partition->id = next_valid_id++;
      }
    }
  }

  // Mark a fragment as having a stack pointer that doesn't necessarily
  // behave like a callstack.
  void MarkAsInvalid(Fragment *frag) {
    if (frag) {
      GRANARY_ASSERT(0 >= frag->partition->id);
      if (!frag->partition->id) {
        frag->partition->id = next_invalid_id--;
      }
    }
  }

  // Try to use information known about the last instruction of the fragment
  // being a control-flow instruction to color a fragment.
  void ColorFragmentByCFI(Fragment *frag) {
    if (auto instr = DynamicCast<ControlFlowInstruction *>(frag->last)) {

      // Assumes that interrupt return, like a function return, reads its
      // target off of the stack.
      if (instr->IsInterruptReturn()) {
        MarkAsValid(frag);
        MarkAsInvalid(frag->fall_through_target);

      // Target block of a system return has an invalid stack.
      } else if (instr->IsSystemReturn()) {
        MarkAsInvalid(frag);
        MarkAsInvalid(frag->fall_through_target);

      // Assumes that function calls/returns push/pop return addresses on the
      // stack. This also makes the assumption that function calls actually
      // lead to returns.
      } else if (instr->IsFunctionCall() || instr->IsFunctionReturn()) {
        MarkAsValid(frag);
        MarkAsValid(frag->successors[1]);
        MarkAsValid(frag->successors[0]);
      }
    }
  }

  // Propagate the coloring from a source fragment to a dest fragment. This
  // can be used for either a successor or predecessor relationship.
  bool PropagateColor(Fragment *source_, Fragment *dest_) {
    auto source = UnsafeCast<CodeFragment *>(source_);
    auto dest = UnsafeCast<CodeFragment *>(dest_);
    if (dest && !dest->partition->id) {
      if (source->block_meta == dest->block_meta &&
          !source->writes_to_stack_pointer &&
          !dest->writes_to_stack_pointer) {
        dest->partition->id = source->partition->id;
        dest->partition.Union(source->partition);
      } else if (source->partition->id > 0) {
        MarkAsValid(dest);
      } else {
        MarkAsInvalid(dest);
      }
      return true;
    }
    return false;
  }

  // Next "invalid stack" partition id.
  int next_invalid_id;

  // Next "valid stack" partition id.
  int next_valid_id;

  // List of all fragments to process.
  Fragment * const frags;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(FragmentColorer);
};

// Partition the fragments into groups, where two fragments belong to the same
// group (partition) iff they are connected by control flow, if they belong to
// the same basic block, and if the stack pointer does not change between them.
void PartitionFragmentsByStackUse(FragmentList *frags) {
  FragmentColorer colorer(frags);
  colorer.Initialize();

  auto first_frag = DynamicCast<CodeFragment *>(frags->First());
  colorer.ColorFragmentByMetaData(first_frag, nullptr);
  /*
  for (auto changed = true; changed; ) {
    changed = colorer.BackPropagate();
    changed = colorer.ForwardPropagate() || changed;

    // If we haven't made progress, then try to take a hint from the meta-data
    // of the entry fragment and propagate it forward (assuming that we have
    // not already deduced the safety of its stack).
    if (!changed && !frags->partition_id) {
      changed = colorer.ColorFragmentByMetaData(frags->First(), nullptr);
    }
  }*/
  colorer.Finalize();
}
#endif

namespace {

// Try to mark some fragment's stack as valid / invalid based on meta-data
// associated with `frag`.
static void AnalyzeFragFromMetadata(Fragment *frag, StackUsageInfo *stack) {
  BlockMetaData *block_meta(nullptr);
  if (auto code = DynamicCast<CodeFragment *>(frag)) {
    block_meta = code->block_meta;
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
          if (succ_cfrag->block_meta == cfrag->block_meta &&
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

// Label the N fragment partitions with IDs 1 through N.
static void LabelPartitions(FragmentList *frags) {
  auto next_id = 1;
  for (auto frag : FragmentIterator(frags)) {
    auto &partition(frag->partition.Value());
    if (!partition.id) {
      partition.id = next_id++;
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
  LabelPartitions(frags);
}

}  // namespace granary
