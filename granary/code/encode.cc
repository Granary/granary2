/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/code/encode.h"

namespace granary {
namespace {

// Stage encode an individual fragment.
static void StageEncode(Fragment *frag, EncodeResult *result) {
  /*
  int num_bytes = 0;
  if (auto code_frag = DynamicCast<CodeFragment *>(frag)) {
    if (code_frag->a)
  }
  for (auto instr : InstructionListIterator)
  */
  GRANARY_UNUSED(frag);
  GRANARY_UNUSED(result);
}

}  // namespace


// Performs stage encoding of a fragment list. This determines the size of each
// fragment and returns the size (in bytes) of the block-specific and edge-
// specific instructions.
EncodeResult StageEncode(FragmentList *frags) {
  EncodeResult result = {0, 0, 0, 0};
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    StageEncode(frag, &result);
  }
  return result;
}

}  // namespace granary
