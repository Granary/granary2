/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/option.h"

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

GRANARY_DEFINE_bool(debug_log_metadata, false,
    "Log the meta-data that is committed to the code cache index. The default "
    "is `no`.");

namespace granary {
namespace {

static void LogBytes(uint64_t *qwords, size_t num_bytes) {
  for (auto i = 0UL; i < num_bytes; ++i) {
    auto qword = qwords[i];
    Log(LogOutput, "%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x ",
        static_cast<unsigned>(qword >> 60) & 0xF,
        static_cast<unsigned>(qword >> 56) & 0xF,
        static_cast<unsigned>(qword >> 52) & 0xF,
        static_cast<unsigned>(qword >> 48) & 0xF,
        static_cast<unsigned>(qword >> 44) & 0xF,
        static_cast<unsigned>(qword >> 40) & 0xF,
        static_cast<unsigned>(qword >> 36) & 0xF,
        static_cast<unsigned>(qword >> 32) & 0xF,

        static_cast<unsigned>(qword >> 28) & 0xF,
        static_cast<unsigned>(qword >> 24) & 0xF,
        static_cast<unsigned>(qword >> 20) & 0xF,
        static_cast<unsigned>(qword >> 16) & 0xF,
        static_cast<unsigned>(qword >> 12) & 0xF,
        static_cast<unsigned>(qword >> 8) & 0xF,
        static_cast<unsigned>(qword >> 4) & 0xF,
        static_cast<unsigned>(qword >> 0) & 0xF);
  }
}

// Add the decoded blocks to the code cache index.
static void IndexBlocks(LockedIndex *index, LocalControlFlowGraph *cfg) {
  LockedIndexTransaction transaction(index);
  if (FLAG_debug_log_metadata) Log(LogOutput, "\n");
  for (auto block : cfg->Blocks()) {
    if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
      auto meta = decoded_block->MetaData();
      if (FLAG_debug_log_metadata) {
        LogBytes(UnsafeCast<uint64_t *>(meta),
                 meta->manager->Size() / sizeof(uint64_t));
        Log(LogOutput, "\n");
      }
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
