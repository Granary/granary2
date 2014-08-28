/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"

#include "granary/code/compile.h"
#include "granary/code/edge.h"
#include "granary/code/metadata.h"

#include "granary/app.h"
#include "granary/breakpoint.h"
#include "granary/cache.h"
#include "granary/context.h"
#include "granary/index.h"
#include "granary/instrument.h"
#include "granary/translate.h"

namespace granary {
namespace {

// Records the number of context switches into Granary.
std::atomic<uint64_t> num_context_switches(ATOMIC_VAR_INIT(0));

// Add the decoded blocks to the code cache index.
static void IndexBlocks(LockedIndex *index, LocalControlFlowGraph *cfg) {
  LockedIndexTransaction transaction(index);
  auto trace_group = num_context_switches.fetch_add(1);
  for (auto block : cfg->Blocks()) {
    if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
      auto meta = decoded_block->MetaData();
      TraceMetaData(trace_group, meta);
      transaction.Insert(meta);
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
  BinaryInstrumenter inst(context, &cfg, &meta);
  inst.InstrumentDirect();

  auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  if (!cache_meta->start_pc) {  // Only compile if we decoded the first block.
    auto index = context->CodeCacheIndex();
    Compile(context, &cfg);
    IndexBlocks(index, &cfg);
  }
  GRANARY_ASSERT(nullptr != cache_meta->start_pc);
  return cache_meta->start_pc;
}

// Instrument, compile, and index some basic blocks, where the entry block
// is targeted by an indirect control-transfer instruction.
CachePC Translate(ContextInterface *context, IndirectEdge *edge,
                  AppPC target_app_pc) {
  auto meta = context->AllocateBlockMetaData(edge->meta_template,
                                             target_app_pc);
  LocalControlFlowGraph cfg(context);
  BinaryInstrumenter inst(context, &cfg, &meta);
  inst.InstrumentIndirect();

  auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  if (!cache_meta->start_pc) {
    auto index = context->CodeCacheIndex();
    Compile(context, &cfg, edge, target_app_pc);
    IndexBlocks(index, &cfg);
  }
  GRANARY_ASSERT(nullptr != cache_meta->start_pc);
  return cache_meta->start_pc;
}

// Instrument, compile, and index some basic blocks that are the entrypoints
// to some native code.
CachePC TranslateEntryPoint(ContextInterface *context, BlockMetaData *meta,
                            EntryPointKind kind, int category) {
  LocalControlFlowGraph cfg(context);
  BinaryInstrumenter inst(context, &cfg, &meta);
  inst.InstrumentEntryPoint(kind, category);

  auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  if (!cache_meta->start_pc) {
    auto index = context->CodeCacheIndex();
    Compile(context, &cfg);
    IndexBlocks(index, &cfg);
  }
  GRANARY_ASSERT(nullptr != cache_meta->start_pc);
  return cache_meta->start_pc;
}

}  // namespace granary
