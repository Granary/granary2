/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/option.h"

#include "granary/code/edge.h"

GRANARY_DEFINE_bool(profile_direct_edges, true,
    "Should all direct edge control-flow transfers be profiled before they "
    "are patched? Default is yes.");

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
