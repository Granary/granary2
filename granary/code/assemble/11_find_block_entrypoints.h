/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_11_FIND_BLOCK_ENTRYPOINTS_H_
#define GRANARY_CODE_ASSEMBLE_11_FIND_BLOCK_ENTRYPOINTS_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Finds the unique first fragment of each block.
void FindBlockEntrypointFragments(FragmentList *frags);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_11_FIND_BLOCK_ENTRYPOINTS_H_
