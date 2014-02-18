/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/code/allocate.h"
#include "granary/code/edge.h"

namespace granary {

// Must be implemented in each Granary driver.
CachePC AssembleEdge(CodeAllocator *allocator, GenericMetaData *meta) {
  (void) meta;
  // TODO(pag): Implement this!
  return allocator->Allocate(GRANARY_ARCH_CACHE_LINE_SIZE, 1);
}

}  // namespace granary
