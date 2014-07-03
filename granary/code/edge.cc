/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/edge.h"

#include "granary/breakpoint.h"
#include "granary/cache.h"
#include "granary/index.h"

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

IndirectEdge::IndirectEdge(void)
    : in_edge(ATOMIC_VAR_INIT(nullptr)),
      encoded_size(0) {}

// Key idea: template meta-datas are never deleted directly, they are only
//           reachable indirectly through another mechanism.
IndirectEdgeMetaData::~IndirectEdgeMetaData(void) {
  if (!target_meta) return;

  BlockMetaData *next_target_meta(nullptr);
  for (; target_meta; target_meta = next_target_meta) {
    auto index_meta = MetaDataCast<IndexMetaData *>(target_meta);
    auto edge_meta = MetaDataCast<IndirectEdgeMetaData *>(target_meta);

    next_target_meta = index_meta->next;
    index_meta->next = nullptr;
    GRANARY_ASSERT(nullptr != edge_meta->edge);
    delete edge_meta->edge;
    edge_meta->edge = nullptr;
    delete target_meta;
  }
}

// Add some indirect block meta-data to the list of indirect block meta-data
// templates targeted by this block.
void IndirectEdgeMetaData::AddIndirectEdge(BlockMetaData *meta) {
  auto index_meta = MetaDataCast<IndexMetaData *>(meta);
  index_meta->next = target_meta;
  target_meta = meta;
}

}  // namespace granary
