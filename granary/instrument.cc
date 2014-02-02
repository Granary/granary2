/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"

#include "granary/instrument.h"
#include "granary/materialize.h"
#include "granary/metadata.h"
#include "granary/tool.h"

namespace granary {
namespace {

// Repeatedly apply LCFG-wide instrumentation for every tool, where tools are
// allowed to materialize direct basic blocks into other forms of basic blocks.
static void InstrumentControlFlow(LocalControlFlowGraph *cfg,
                                  GenericMetaData *meta) {
  Materializer materializer(cfg);
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

// Test the instrumentation system.
static void TestInstrument(void) {
  LocalControlFlowGraph cfg;
  auto meta = new GenericMetaData(
      UnsafeCast<AppProgramCounter>(&InstrumentControlFlow));
  Instrument(&cfg, std::move(std::unique_ptr<GenericMetaData>(meta)));
}

}  // namespace

// Take over a program's execution by replacing a return address with an
// instrumented return address.
void Instrument(LocalControlFlowGraph *cfg,
                std::unique_ptr<GenericMetaData> meta) {
  InstrumentControlFlow(cfg, meta.release());
  InstrumentBlocks(cfg);
  InstrumentBlock(cfg);
}

// Initialize the instrumentation system. This goes and checks if any tools
// are defined that might actually want to instrument code in one way or
// another.
void InitInstrumentation(void) {


  TestInstrument();  // TODO(pag): Remove me.
}

}  // namespace granary
