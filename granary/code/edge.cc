/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/edge.h"

#include "granary/metadata.h"

namespace granary {

DirectEdge::DirectEdge(const BlockMetaData *source_meta_,
                       BlockMetaData *dest_meta_, CachePC edge_code_)
    : entry_target(nullptr),
      exit_target(edge_code_),
      num_executions(0),
      num_execution_overflows(0),
      next(nullptr),
      source_meta(source_meta_),
      dest_meta(ATOMIC_VAR_INIT(dest_meta_)),
      edge_code(edge_code_),
      patch_instruction(nullptr) {}

DirectEdge::~DirectEdge(void) {
  if (auto meta = dest_meta.exchange(nullptr, std::memory_order_relaxed)) {
    delete meta;
  }
}

}  // namespace granary
