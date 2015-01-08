/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/option.h"

#include "granary/cfg/instruction.h"
#include "granary/cfg/iterator.h"

#include "granary/code/fragment.h"
#include "granary/code/assemble/3_partition_fragments.h"

#include "granary/breakpoint.h"
#include "granary/util.h"

#ifdef GRANARY_WHERE_user
GRANARY_DEFINE_bool(try_spill_VRs_to_stack, GRANARY_IF_TEST_ELSE(false, true),
    "Should Granary try to spill virtual registers onto the call stack? The "
    "default is `yes`.\n"
    "\n"
    "Note: Disabling this is a good way to test if stack spilling/filling is\n"
    "      the cause of a bug.");
#endif  // GRANARY_WHERE_user

namespace granary {
namespace {

// Initializes the stack validity analysis.
static void InitStackValidity(FragmentList *frags) {
  auto valid = GRANARY_IF_USER_ELSE(FLAG_try_spill_VRs_to_stack, true);
  for (auto frag : FragmentListIterator(frags)) {
    if (kStackStatusInvalid == frag->stack_status) {
      valid = false;
      break;
    }
  }
  if (valid) return;
  for (auto frag : FragmentListIterator(frags)) {
    frag->stack_status = kStackStatusInvalid;
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

          // Can't put this fragment into the same partition as any of its
          // predecessors. This happens if this fragment is the fall-through of
          // a control-flow instruction.
          if (!succ_cfrag->attr.can_add_pred_to_partition) continue;

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
  InitStackValidity(frags);
  GroupFragments(frags);
}

}  // namespace granary
