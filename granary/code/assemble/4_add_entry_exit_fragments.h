/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_4_ADD_ENTRY_EXIT_FRAGMENTS_H_
#define GRANARY_CODE_ASSEMBLE_4_ADD_ENTRY_EXIT_FRAGMENTS_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

class LocalControlFlowGraph;

// Adds designated entry and exit fragments around fragment partitions and
// around groups of instrumentation code fragments. First we add entry/exits
// around instrumentation code fragments for saving/restoring flags, then we
// add entry/exits around the partitions for saving/restoring registers.
void AddEntryAndExitFragments(LocalControlFlowGraph *cfg, FragmentList *frags);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_4_ADD_ENTRY_EXIT_FRAGMENTS_H_
