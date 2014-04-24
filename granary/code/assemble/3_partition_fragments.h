/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_3_PARTITION_FRAGMENTS_H_
#define GRANARY_CODE_ASSEMBLE_3_PARTITION_FRAGMENTS_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Partition the fragments into groups, where each group is labeled/colored by
// their `stack_id` field.
void PartitionFragmentsByStackUse(FragmentList *frags);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_3_PARTITION_FRAGMENTS_H_
