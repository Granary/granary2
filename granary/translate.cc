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
std::atomic<uint64_t> gNumContextSwitches(ATOMIC_VAR_INIT(0));

enum IndexConstraint {
  // Index all basic blocks in the control-flow graph.
  kIndexAll,

  // Index all blocks except for the entry block. This is used for indirect
  // edges because we expect those to be `CompensationBasicBlocks` that
  // are likely to be edge-specific, and we also don't want to pollute the
  // cache with such blocks because then we'll see one compensation block
  // pointing to another pointing to another... and then eventually getting
  // to the intended destination block.
  kIndexAllButEntry
};

// Add the decoded blocks to the code cache index.
static void IndexBlocks(LockedIndex *index, LocalControlFlowGraph *cfg,
                        IndexConstraint constraint=kIndexAll) {
  const auto entry_block = cfg->EntryBlock();
  GRANARY_ASSERT(nullptr != entry_block);

  LockedIndexTransaction transaction(index);
  auto trace_group = gNumContextSwitches.fetch_add(1);
  for (auto block : cfg->ReverseBlocks()) {
    if (kIndexAllButEntry == constraint && entry_block == block) continue;
    if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
      GRANARY_ASSERT(nullptr != decoded_block->StartAppPC());
      auto meta = decoded_block->MetaData();
      transaction.Insert(meta);
      TraceMetaData(trace_group, meta);
    }
  }
}

// Compile and index blocks. This is used for direct edges and entrypoints.
static CachePC CompileAndIndex(Context *context, LocalControlFlowGraph *cfg,
                               BlockMetaData *meta) {
  auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  if (!cache_meta->start_pc) {  // Only compile if we decoded the first block.
    auto index = context->CodeCacheIndex();
    Compile(context, cfg);
    IndexBlocks(index, cfg, kIndexAll);
  }
  GRANARY_ASSERT(nullptr != cache_meta->start_pc);
  return cache_meta->start_pc;
}

static void MarkStack(BlockMetaData *meta, TargetStackValidity stack_valid) {
  if (kTargetStackValid == stack_valid) {
    auto stack_meta = MetaDataCast<StackMetaData *>(meta);
    stack_meta->MarkStackAsValid();
  }
}

}  // namespace

// Instrument, compile, and index some basic blocks.
CachePC Translate(Context *context, AppPC pc, TargetStackValidity stack_valid) {
  auto meta = context->AllocateBlockMetaData(pc);
  MarkStack(meta, stack_valid);
  return Translate(context, meta);
}

// Instrument, compile, and index some basic blocks.
CachePC Translate(Context *context, BlockMetaData *meta) {
  LocalControlFlowGraph cfg(context);
  BinaryInstrumenter inst(context, &cfg, &meta);
  inst.InstrumentDirect();
  return CompileAndIndex(context, &cfg, meta);
}

// Instrument, compile, and index some basic blocks, where the entry block
// is targeted by an indirect control-transfer instruction.
CachePC Translate(Context *context, IndirectEdge *edge, AppPC target_app_pc) {
  auto meta = context->AllocateBlockMetaData(edge->meta_template,
                                             target_app_pc);
  LocalControlFlowGraph cfg(context);
  BinaryInstrumenter inst(context, &cfg, &meta);
  inst.InstrumentIndirect();

  auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  if (!cache_meta->start_pc) {
    auto index = context->CodeCacheIndex();
    Compile(context, &cfg, edge, target_app_pc);
    IndexBlocks(index, &cfg, kIndexAllButEntry);
  }
  GRANARY_ASSERT(nullptr != cache_meta->start_pc);
  return cache_meta->start_pc;
}

// Instrument, compile, and index some basic blocks that are the entrypoints
// to some native code.
CachePC TranslateEntryPoint(Context *context, BlockMetaData *meta,
                            EntryPointKind kind,
                            TargetStackValidity stack_valid, int category) {
  LocalControlFlowGraph cfg(context);
  BinaryInstrumenter inst(context, &cfg, &meta);
  MarkStack(meta, stack_valid);
  inst.InstrumentEntryPoint(kind, category);
  return CompileAndIndex(context, &cfg, meta);
}

// Instrument, compile, and index some basic blocks that are the entrypoints
// to some native code.
CachePC TranslateEntryPoint(Context *context, AppPC target_pc,
                            EntryPointKind kind,
                            TargetStackValidity stack_valid, int category) {
  auto meta = context->AllocateBlockMetaData(target_pc);
  return TranslateEntryPoint(context, meta, kind, stack_valid, category);
}

}  // namespace granary
