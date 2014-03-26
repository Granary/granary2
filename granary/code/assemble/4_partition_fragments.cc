/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"

#include "granary/code/assemble/fragment.h"
#include "granary/code/assemble/4_partition_fragments.h"

#include "granary/code/metadata.h"

#include "granary/breakpoint.h"
#include "granary/util.h"

namespace granary {

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
  bool ColorFragmentByMetaData(Fragment *frag, Fragment *first_frag) {
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
    for (auto frag : FragmentIterator(frags)) {
      if (frag->reads_from_stack_pointer) {  // Reads & writes the stack pointer.
        MarkAsValid(frag);
      } else if (frag->block_meta && frag->is_exit) {
        ColorFragmentByMetaData(frag, frags);
      }
      ColorFragmentByCFI(frag);
    }
  }

  // Finalize the fragment coloring.
  void Finalize(void) {
    for (auto frag : FragmentIterator(frags)) {
      if (!frag->partition_id) {
        MarkAsInvalid(frag);
      }
    }
  }

  // Perform a backward data-flow pass on the fragment stack ID colorings.
  bool BackPropagate(void) {
    auto global_changed = false;
    for (auto changed = true; changed; ) {
      changed = false;
      for (auto frag : FragmentIterator(frags)) {
        if (!frag->partition_id &&
            !frag->writes_to_stack_pointer &&
            frag->fall_through_target &&
            frag->fall_through_target->partition_id) {
          changed = PropagateColor(frag->fall_through_target, frag) || changed;
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
      for (auto frag : FragmentIterator(frags)) {
        if (!frag->partition_id || frag->writes_to_stack_pointer) {
          continue;
        }
        changed = PropagateColor(frag, frag->branch_target) || changed;
        changed = PropagateColor(frag, frag->fall_through_target) || changed;
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
      GRANARY_ASSERT(0 <= frag->partition_id);
      if (!frag->partition_id) {
        frag->partition_id = next_valid_id++;
      }
    }
  }

  // Mark a fragment as having a stack pointer that doesn't necessarily
  // behave like a callstack.
  void MarkAsInvalid(Fragment *frag) {
    if (frag) {
      GRANARY_ASSERT(0 >= frag->partition_id);
      if (!frag->partition_id) {
        frag->partition_id = next_invalid_id--;
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
        MarkAsValid(frag->branch_target);
        MarkAsValid(frag->fall_through_target);
      }
    }
  }

  // Propagate the coloring from a source fragment to a dest fragment. This
  // can be used for either a successor or predecessor relationship.
  bool PropagateColor(Fragment *source, Fragment *dest) {
    if (dest && !dest->partition_id) {
      if (source->block_meta == dest->block_meta) {
        dest->partition_id = source->partition_id;
      } else if (source->partition_id > 0) {
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

// Partition the fragments into groups, where each group is labeled/colored by
// their `stack_id` field.
void PartitionFragmentsByStackUse(Fragment * const frags) {
  FragmentColorer colorer(frags);
  colorer.Initialize();
  for (auto changed = true; changed; ) {
    changed = colorer.BackPropagate();
    changed = colorer.ForwardPropagate() || changed;

    // If we haven't made progress, then try to take a hint from the meta-data
    // of the entry fragment and propagate it forward (assuming that we have
    // not already deduced the safety of its stack).
    if (!changed && !frags->partition_id) {
      changed = colorer.ColorFragmentByMetaData(frags, nullptr);
    }
  }
  colorer.Finalize();
}


}  // namespace granary
