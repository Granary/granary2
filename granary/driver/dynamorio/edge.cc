/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/edge.h"

namespace granary {

// Must be implemented in each Granary driver.
CacheProgramCounter AssembleEdge(CodeAllocator *allocator,
                                 GenericMetaData *meta) {
  (void) allocator;
  (void) meta;
  return nullptr;
}

}  // namespace granary
