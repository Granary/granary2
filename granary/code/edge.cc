/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/edge.h"

#include "granary/breakpoint.h"
#include "granary/cache.h"
#include "granary/index.h"
#include "granary/metadata.h"

namespace granary {

DirectEdge::DirectEdge(BlockMetaData *dest_meta_, CachePC edge_code_)
    : entry_target(nullptr),
      exit_target(edge_code_),
      num_executions(0),
      next(nullptr),
      dest_meta(ATOMIC_VAR_INIT(dest_meta_)),
      edge_code(edge_code_),
      patch_instruction(nullptr) {}

DirectEdge::~DirectEdge(void) {
  if (auto meta = dest_meta.exchange(nullptr, std::memory_order_relaxed)) {
    delete meta;
  }
}

IndirectEdge::IndirectEdge(const BlockMetaData *dest_meta_,
                           CachePC indirect_edge_entrypoint)
    : out_edge_pc(indirect_edge_entrypoint),
      out_edge_pc_lock(),
      meta_template(dest_meta_),
      next(nullptr),
      begin_out_edge_template(nullptr),
      end_out_edge_template(nullptr) {}

IndirectEdge::~IndirectEdge(void) {
  delete meta_template;
}

}  // namespace granary
