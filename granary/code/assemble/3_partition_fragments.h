/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_3_PARTITION_FRAGMENTS_H_
#define GRANARY_CODE_ASSEMBLE_3_PARTITION_FRAGMENTS_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Partition the fragments into groups, where two fragments belong to the same
// group (partition) iff they are connected by control flow, if they belong to
// the same basic block, and if the stack pointer does not change between them.
void PartitionFragments(FragmentList *frags);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_3_PARTITION_FRAGMENTS_H_
