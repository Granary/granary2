/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/fragment.h"

#include "granary/code/assemble/11_find_block_entrypoints.h"

namespace granary {

// Finds the unique first fragment of each block.
void FindBlockEntrypointFragments(FragmentList *frags) {
  // Find the unique block head.
  for (auto frag : FragmentListIterator(frags)) {
    if (auto cfrag = DynamicCast<CodeFragment *>(frag)) {
      if (!(cfrag->attr.is_block_head || cfrag->attr.is_return_target)) {
        continue;
      }
      auto partition = cfrag->partition.Value();
      GRANARY_ASSERT(!partition->entry_frag);
      partition->entry_frag = frag;
    }
  }

  // Find the head of the partition that contains the unique block head, if
  // such a head exists.
  for (auto frag : FragmentListIterator(frags)) {
    if (IsA<PartitionEntryFragment *>(frag)) {
      auto partition = frag->partition.Value();
      if (partition->entry_frag) {
        partition->entry_frag = frag;
      }
    }
  }
}

}  // namespace granary
