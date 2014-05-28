/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/encode.h"

#include "granary/code/encode.h"

namespace granary {
namespace {

// Stage encode an individual fragment.
static CachePC StageEncode(Fragment *frag, EncodeResult *result,
                           CachePC encode_addr) {
  arch::InstructionEncoder encoder(arch::InstructionEncodeKind::STAGED);
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if (!ninstr->instruction.IsNoOp()) {
        GRANARY_IF_DEBUG( bool encoded = ) encoder.EncodeNext(
            &(ninstr->instruction), &encode_addr);
        GRANARY_ASSERT(encoded);
      }
    } else if (auto annot = DynamicCast<AnnotationInstruction *>(instr)) {
      if (IA_LABEL == annot->annotation ||
          IA_RETURN_ADDRESS == annot->annotation) {
        annot->data = reinterpret_cast<uintptr_t>(encode_addr);
      }
    }
  }
  GRANARY_UNUSED(result);

  return encode_addr;
}

}  // namespace


// Performs stage encoding of a fragment list. This determines the size of each
// fragment and returns the size (in bytes) of the block-specific and edge-
// specific instructions.
EncodeResult StageEncode(FragmentList *frags) {
  EncodeResult result = {0, 0, 0, 0};
  CachePC encode_addr(nullptr);
  for (auto frag : EncodeOrderedFragmentIterator(frags->First())) {
    encode_addr = StageEncode(frag, &result, encode_addr);
  }
  return result;
}

}  // namespace granary
