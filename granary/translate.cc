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

#include "granary/logging.h"

namespace granary {
namespace {

// Add the decoded blocks to the code cache index.
static void IndexBlocks(LockedIndex *index, LocalControlFlowGraph *cfg) {
  LockedIndexTransaction transaction(index);
  for (auto block : cfg->Blocks()) {
    if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {

      auto meta = decoded_block->MetaData();
      auto app_meta = MetaDataCast<AppMetaData *>(meta);
      auto cache_meta = MetaDataCast<CacheMetaData *>(meta);

      Log(LogOutput, "0x%p 0x%p\n", app_meta->start_pc, cache_meta->start_pc);

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
  CacheMetaData *cache_meta(nullptr);
  meta = Instrument(context, &cfg, meta, INSTRUMENT_DIRECT);
  cache_meta = MetaDataCast<CacheMetaData *>(meta);
  if (!cache_meta->start_pc) {  // Only compile if we decoded the first block.
    auto index = context->CodeCacheIndex();
    Compile(context, &cfg);
    IndexBlocks(index, &cfg);
  } else {
    GRANARY_USED(cache_meta->start_pc);
  }
  GRANARY_ASSERT(nullptr != cache_meta->start_pc);
  return cache_meta->start_pc;
}

// Instrument, compile, and index some basic blocks, where the entry block
// is targeted by an indirect control-transfer instruction.
CachePC Translate(ContextInterface *context, IndirectEdge *edge,
                  AppPC target_app_pc) {
  LocalControlFlowGraph cfg(context);
  auto meta = context->AllocateBlockMetaData(edge->meta_template,
                                             target_app_pc);
  meta = Instrument(context, &cfg, meta, INSTRUMENT_INDIRECT);
  auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  if (!cache_meta->start_pc) {
    auto index = context->CodeCacheIndex();
    Compile(context, &cfg, edge, target_app_pc);
    IndexBlocks(index, &cfg);
  } else {
    GRANARY_USED(cache_meta->start_pc);
  }
  GRANARY_ASSERT(nullptr != cache_meta->start_pc);
  return cache_meta->start_pc;
}

}  // namespace granary
