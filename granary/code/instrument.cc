/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/factory.h"

#include "granary/code/instrument.h"

#include "granary/metadata.h"
#include "granary/tool.h"

#include "dependencies/xxhash/hash.h"

namespace granary {
namespace {

// Repeatedly apply LCFG-wide instrumentation for every tool, where tools are
// allowed to materialize direct basic blocks into other forms of basic blocks.
static void InstrumentControlFlow(Environment *env, LocalControlFlowGraph *cfg,
                                  GenericMetaData *meta) {
  BlockFactory materializer(env, cfg);
  materializer.MaterializeInitialBlock(meta);
  for (;; materializer.MaterializeRequestedBlocks()) {
    for (auto tool : Tools()) {
      tool->InstrumentControlFlow(&materializer, cfg);
    }
    if (!materializer.HasPendingMaterializationRequest()) {
      break;
    }
  }
}

// Apply LCFG-wide instrumentation for every tool.
static void InstrumentBlocks(LocalControlFlowGraph *cfg) {
  for (auto tool : Tools()) {
    tool->InstrumentBlocks(cfg);
  }
}

// Apply instrumentation to every block for every tool.
//
// Note: This applies tool-specific instrumentation for all tools to a single
//       block before moving on to the next block in the LCFG.
static void InstrumentBlock(LocalControlFlowGraph *cfg) {
  for (auto block : cfg->Blocks()) {
    for (auto tool : Tools()) {
      auto decoded_block = DynamicCast<DecodedBasicBlock *>(block);
      if (decoded_block) {
        tool->InstrumentBlock(decoded_block);
      }
    }
  }
}

static uint32_t HashMetaData(GenericMetaData *meta) {
  xxhash::HashFunction hasher(0xDEADBEEFUL);
  hasher.Reset();
  meta->Hash(&hasher);
  hasher.Finalize();
  return hasher.Extract32();
}

}  // namespace

// Take over a program's execution by replacing a return address with an
// instrumented return address.
void Instrument(Environment *env, LocalControlFlowGraph *cfg,
                GenericMetaData *meta) {
  auto meta_hash = HashMetaData(meta);

  InstrumentControlFlow(env, cfg, meta);
  InstrumentBlocks(cfg);
  InstrumentBlock(cfg);

  // Verify that the indexable meta-data for the entry basic block has not
  // changed during the instrumentation process.
  granary_break_on_fault_if(HashMetaData(meta) != meta_hash);
}

}  // namespace granary
