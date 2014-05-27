/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ENCODE_H_
#define GRANARY_CODE_ENCODE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/code/fragment.h"

namespace granary {

struct EncodeResult {
  int num_block_bytes;
  int num_direct_edge_bytes;
  int num_in_edge_bytes;
  int num_out_edge_bytes;
};

// Performs stage encoding of a fragment list. This determines the size of each
// fragment and returns the size (in bytes) of the block-specific and edge-
// specific instructions.
EncodeResult StageEncode(FragmentList *frags);

}  // namespace granary

#endif  // GRANARY_CODE_ENCODE_H_
