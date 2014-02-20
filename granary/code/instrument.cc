/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/factory.h"

#include "granary/metadata.h"
#include "granary/tool.h"

#include "granary/code/instrument.h"

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

}  // namespace

// Take over a program's execution by replacing a return address with an
// instrumented return address.
void Instrument(Environment *env, LocalControlFlowGraph *cfg,
                GenericMetaData *meta) {
  InstrumentControlFlow(env, cfg, meta);
  InstrumentBlocks(cfg);
  InstrumentBlock(cfg);
}

}  // namespace granary
