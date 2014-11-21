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

// In some cases, some blocks can become completely unreachable (via control
// flow). In these cases, no code is generated for these blocks, and so delete
// their meta-datas.
static void ReapUnreachableBlocks(LocalControlFlowGraph *cfg) {
  for (auto block : cfg->Blocks()) {
    if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
      if (!decoded_block->StartCachePC()) {
        delete decoded_block->meta;
        decoded_block->meta = nullptr;
      }
    }
  }
}

// Add the decoded blocks to the code cache index.
static void IndexBlocks(LockedIndex *index, LocalControlFlowGraph *cfg) {
  LockedIndexTransaction transaction(index);
  auto trace_group = gNumContextSwitches.fetch_add(1);
  auto reap_unreachable = false;
  for (auto block : cfg->Blocks()) {
    if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
      // TODO(pag): Should we omit non-comparable compensation basic blocks
      //            from the index?
      if (decoded_block->StartCachePC()) {
        auto meta = decoded_block->MetaData();
        TraceMetaData(trace_group, meta);
        transaction.Insert(meta);
      } else {
        reap_unreachable = true;
      }
    }
  }
  if (reap_unreachable) ReapUnreachableBlocks(cfg);
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
    IndexBlocks(index, &cfg);
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

  auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  if (!cache_meta->start_pc) {
    auto index = context->CodeCacheIndex();
    Compile(context, &cfg);
    IndexBlocks(index, &cfg);
  }
  GRANARY_ASSERT(nullptr != cache_meta->start_pc);
  return cache_meta->start_pc;
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
