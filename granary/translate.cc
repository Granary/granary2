/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/block.h"
#include "granary/cfg/trace.h"

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

// Add the trace entrypoint to the index.
static void IndexEntryBlock(Index *index, Trace *cfg) {
  const auto entry_block = cfg->EntryBlock();
  GRANARY_ASSERT(nullptr != entry_block);

  auto meta = entry_block->MetaData();
  TraceMetaData(meta);

  // Only index the meta-data if there's not already some suitable meta-data in
  // the index.
  auto response = index->Request(meta);
  if (kUnificationStatusAccept != response.status) {
    index->Insert(meta);
  }
}

// Compile and index blocks. This is used for direct edges and entrypoints.
static CachePC CompileAndIndex(Context *context, Trace *trace,
                               BlockMetaData *meta) {
  auto cache_meta = MetaDataCast<CacheMetaData *>(meta);
  if (!cache_meta->start_pc) {  // Only compile if we decoded the first block.
    auto encoded_pc = Compile(context, trace);

    auto index = context->CodeCacheIndex();
    IndexEntryBlock(index, trace);
    GRANARY_ASSERT(nullptr != cache_meta->start_pc);
    return encoded_pc;
  } else {
    return cache_meta->start_pc;
  }
}

// Mark the stack as being valid, i.e. behaving like a C-style call stack with
// call/return and push/pop semantics, or as being unknown.
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
  Trace cfg(context);
  BinaryInstrumenter inst(context, &cfg, &meta);
  inst.InstrumentDirect();
  return CompileAndIndex(context, &cfg, meta);
}

// Instrument, compile, and index some basic blocks, where the entry block
// is targeted by an indirect control-transfer instruction.
//
// This is special because we need to do a few things:
//      1) We need to make a compensation fragment that directly jumps to
//         `target_app_pc`.
//      2) We need to set up the compensation fragment such that the direct
//         jump has a default non-`kRequestBlockInFuture` materialization
//         strategy.
//      3) We need to prepend the out-edge code to the resulting code (by
//         "instantiating" the out edge into a fragment).
CachePC Translate(Context *context, IndirectEdge *edge, BlockMetaData *meta) {
  Trace cfg(context);
  BinaryInstrumenter inst(context, &cfg, &meta);
  inst.InstrumentIndirect();
  auto encoded_pc = Compile(context, &cfg, edge, meta);
  auto index = context->CodeCacheIndex();
  IndexEntryBlock(index, &cfg);
  return encoded_pc;
}

// Instrument, compile, and index some basic blocks that are the entrypoints
// to some native code.
CachePC TranslateEntryPoint(Context *context, BlockMetaData *meta,
                            EntryPointKind kind, int category,
                            TargetStackValidity stack_valid) {
  Trace cfg(context);
  BinaryInstrumenter inst(context, &cfg, &meta);
  MarkStack(meta, stack_valid);
  inst.InstrumentEntryPoint(kind, category);
  return CompileAndIndex(context, &cfg, meta);
}

// Instrument, compile, and index some basic blocks that are the entrypoints
// to some native code.
CachePC TranslateEntryPoint(Context *context, AppPC target_pc,
                            EntryPointKind kind, int category,
                            TargetStackValidity stack_valid) {
  auto meta = context->AllocateBlockMetaData(target_pc);
  return TranslateEntryPoint(context, meta, kind, category, stack_valid);
}

}  // namespace granary
