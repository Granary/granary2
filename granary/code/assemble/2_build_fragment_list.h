/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_2_BUILD_FRAGMENT_LIST_H_
#define GRANARY_CODE_ASSEMBLE_2_BUILD_FRAGMENT_LIST_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Forward declarations.
class LocalControlFlowGraph;

// Build a fragment list out of a set of basic blocks.
void BuildFragmentList(LocalControlFlowGraph *cfg, FragmentList *frags);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_2_BUILD_FRAGMENT_LIST_H_
