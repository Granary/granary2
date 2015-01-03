/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/edge.h"

#include "granary/breakpoint.h"
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

IndirectEdge::IndirectEdge(const BlockMetaData *source_meta_,
                           const BlockMetaData *dest_meta_)
    : out_edge_pc(nullptr),
      source_block_meta(source_meta_),
      dest_block_meta_template(dest_meta_),
      next(nullptr),
      out_edge_template(nullptr),
      out_edges(),
      lock() {}

IndirectEdge::~IndirectEdge(void) {
  delete dest_block_meta_template;
}

}  // namespace granary
