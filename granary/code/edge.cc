/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/edge.h"

#include "granary/breakpoint.h"
#include "granary/cache.h"
#include "granary/index.h"
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

IndirectEdge::IndirectEdge(const BlockMetaData *source_meta_,
                           const BlockMetaData *dest_meta_,
                           CachePC indirect_edge_entrypoint)
    : out_edge_pc(indirect_edge_entrypoint),
      out_edge_pc_lock(),
      source_meta(source_meta_),
      dest_meta(dest_meta_),
      next(nullptr),
      begin_out_edge_template(nullptr),
      end_out_edge_template(nullptr) {}

IndirectEdge::~IndirectEdge(void) {
  delete dest_meta;
}

}  // namespace granary
