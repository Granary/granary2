/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/edge.h"
#include "granary/context.h"

namespace granary {

// Must be implemented in each Granary driver.
CachePC AssembleEdge(ContextInterface *env, BlockMetaData *meta) {
  (void) meta;
  // TODO(pag): Implement this!
  return env->AllocateEdgeCode(1);
}

}  // namespace granary
