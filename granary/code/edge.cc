/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/edge.h"

namespace granary {

DirectEdge::DirectEdge(ContextInterface *context_,
                       const BlockMetaData *source_meta_,
                       BlockMetaData *dest_meta_, CachePC edge_code_)
    : cached_target(nullptr),
      num_executions(0),
      num_execution_overflows(0),
      context(context_),
      next(nullptr),
      source_meta(source_meta_),
      dest_meta(dest_meta_),
      edge_code(edge_code_),
      patch_instruction(nullptr) {}

}  // namespace granary
