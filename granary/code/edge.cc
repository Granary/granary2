/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/edge.h"

#include "granary/breakpoint.h"
#include "granary/cache.h"
#include "granary/index.h"
#include "granary/metadata.h"

namespace granary {

DirectEdge::DirectEdge(BlockMetaData *dest_meta_, CachePC edge_code_,
                       DirectEdge *next_)
    : entry_target_pc(nullptr),
      next(next_),
      dest_meta(dest_meta_),
      edge_code_pc(edge_code_),
      patch_instruction_pc(nullptr),
      lock() {}

DirectEdge::~DirectEdge(void) {
  if (dest_meta) delete dest_meta;
}

IndirectEdge::IndirectEdge(const BlockMetaData *dest_meta_,
                           CachePC indirect_edge_entrypoint)
    : out_edge_pc(indirect_edge_entrypoint),
      meta_template(dest_meta_),
      next(nullptr),
      out_edge_template(nullptr),
      out_edges(),
      lock() {}

IndirectEdge::~IndirectEdge(void) {
  delete meta_template;
}

}  // namespace granary
