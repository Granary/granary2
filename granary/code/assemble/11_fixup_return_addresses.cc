/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/fragment.h"

#include "granary/code/assemble/11_fixup_return_addresses.h"

namespace granary {
namespace {

// For each basic block, this finds the unique first fragment of the block.
static void FindBlockEntrypointFragments(FragmentList *frags) {
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

}  // namespace

// Makes sure that all `IA_RETURN_ADDRESS` annotations are in the correct
// position.
void FixupReturnAddresses(FragmentList *frags) {
  FindBlockEntrypointFragments(frags);
  for (auto frag : FragmentListIterator(frags)) {
    for (auto instr : InstructionListIterator(frag->instrs)) {
      if (auto annot_instr = DynamicCast<AnnotationInstruction *>(instr)) {
        if (IA_RETURN_ADDRESS == annot_instr->annotation) {
          auto entry_frag = frag->partition.Value()->entry_frag;
          frag->instrs.Remove(instr);
          entry_frag->instrs.Prepend(instr);
          break;
        }
      }
    }
  }
}

}  // namespace granary
