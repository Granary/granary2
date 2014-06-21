/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"

#include "granary/code/compile.h"

#include "granary/breakpoint.h"
#include "granary/cache.h"
#include "granary/context.h"
#include "granary/dispatch.h"
#include "granary/index.h"
#include "granary/instrument.h"

namespace granary {
namespace {

// Add the decoded blocks to the code cache index.
static void IndexBlocks(LockedIndex *index, LocalControlFlowGraph *cfg) {
  auto transaction(index->Transaction());
  for (auto block : cfg->Blocks()) {
    if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
      transaction.Insert(decoded_block->MetaData());
    }
  }
}

}  // namespace

// Instrument, compile, and index some basic blocks.
CachePC Dispatch(ContextInterface *context, AppPC pc) {
  return Dispatch(context, context->AllocateBlockMetaData(pc));
}

// Instrument, compile, and index some basic blocks.
CachePC Dispatch(ContextInterface *context, BlockMetaData *meta) {
  LocalControlFlowGraph cfg(context);
  auto index = context->CodeCacheIndex();
  meta = Instrument(context, &cfg, meta);
  auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  if (!cache_meta->cache_pc) {  // Only compile if we decoded the first block.
    Compile(&cfg);
    IndexBlocks(index, &cfg);
  }
  GRANARY_ASSERT(nullptr != cache_meta->cache_pc);
  return cache_meta->cache_pc;
}

}  // namespace granary
