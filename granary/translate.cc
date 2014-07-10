/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"

#include "granary/code/compile.h"
#include "granary/code/edge.h"
#include "granary/code/metadata.h"

#include "granary/breakpoint.h"
#include "granary/cache.h"
#include "granary/context.h"
#include "granary/index.h"
#include "granary/instrument.h"
#include "granary/translate.h"

namespace granary {
namespace {

// Add the decoded blocks to the code cache index.
static void IndexBlocks(LockedIndex *index, LocalControlFlowGraph *cfg) {
  LockedIndexTransaction transaction(index);
  for (auto block : cfg->Blocks()) {
    if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
      transaction.Insert(decoded_block->MetaData());
    }
  }
}

}  // namespace

// Instrument, compile, and index some basic blocks.
CachePC Translate(ContextInterface *context, AppPC pc,
                  TargetStackValidity stack_valid) {
  auto meta = context->AllocateBlockMetaData(pc);
  if (TRANSLATE_STACK_VALID == stack_valid) {
    auto stack_meta = MetaDataCast<StackMetaData *>(meta);
    stack_meta->MarkStackAsValid();
  }
  return Translate(context, meta);
}

// Instrument, compile, and index some basic blocks.
CachePC Translate(ContextInterface *context, BlockMetaData *meta) {
  LocalControlFlowGraph cfg(context);
  auto index = context->CodeCacheIndex();
  meta = Instrument(context, &cfg, meta, INSTRUMENT_DIRECT);
  auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  if (!cache_meta->cache_pc) {  // Only compile if we decoded the first block.
    Compile(context, &cfg);
    IndexBlocks(index, &cfg);
  }
  GRANARY_ASSERT(nullptr != cache_meta->cache_pc);
  return cache_meta->cache_pc;
}

// Instrument, compile, and index some basic blocks, where the entry block
// is targeted by an indirect control-transfer instruction.
CachePC Translate(ContextInterface *context, IndirectEdge *edge,
                  AppPC target_app_pc) {
  LocalControlFlowGraph cfg(context);
  auto index = context->CodeCacheIndex();
  auto meta = context->AllocateBlockMetaData(edge->dest_meta, target_app_pc);
  meta = Instrument(context, &cfg, meta, INSTRUMENT_INDIRECT);
  auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  if (!cache_meta->cache_pc) {  // Only compile if we decoded the first block.
    Compile(context, &cfg, edge, target_app_pc);
    IndexBlocks(index, &cfg);
  }
  GRANARY_ASSERT(nullptr != cache_meta->cache_pc);
  return cache_meta->cache_pc;
}

}  // namespace granary
