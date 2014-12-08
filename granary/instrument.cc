/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/option.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/factory.h"

#include "granary/instrument.h"

#include "granary/breakpoint.h"
#include "granary/context.h"
#include "granary/metadata.h"
#include "granary/tool.h"

GRANARY_DEFINE_positive_int(max_num_control_flow_iterations, 8,
    "The maximum number of iterations of the control-flow instrumentation "
    "pass per trace request. The default value is `8`, which--despite being "
    "small--could result in a massive blowup of code.");

namespace granary {

// Initialize a binary instrumenter.
BinaryInstrumenter::BinaryInstrumenter(Context *context_,
                                       LocalControlFlowGraph *cfg_,
                                       BlockMetaData **meta_)
    : context(context_),
      tools(AllocateTools()),
      meta(meta_),
      cfg(cfg_),
      factory(context, cfg) {}

BinaryInstrumenter::~BinaryInstrumenter(void) {
  FreeTools(tools);
}

// Instrument some code as-if it is targeted by a direct CFI.
void BinaryInstrumenter::InstrumentDirect(void) {
  auto entry_block = factory.RequestDirectEntryBlock(meta);
  if (!entry_block) {  // Couldn't find or adapt to a existing block.
    entry_block = factory.MaterializeDirectEntryBlock(*meta);
  }

  *meta = nullptr;  // Potentially undefined after this point.

  if (IsA<DecodedBasicBlock *>(entry_block)) {  // Instrument decoded blocks.
    InstrumentControlFlow();
    InstrumentBlocks();
    InstrumentBlock();
    factory.RemoveUnreachableBlocks();
  }

  *meta = entry_block->UnsafeMetaData();
}

// Instrument some code as-if it is targeted by an indirect CFI.
void BinaryInstrumenter::InstrumentIndirect(void) {
  factory.MaterializeIndirectEntryBlock(*meta);
  InstrumentControlFlow();
  InstrumentBlocks();
  InstrumentBlock();
  factory.RemoveUnreachableBlocks();
}

// Instrument some code as-if it is targeted by a native entrypoint. These
// are treated as being the initial points of instrumentation.
void BinaryInstrumenter::InstrumentEntryPoint(EntryPointKind kind,
                                              int category) {
  factory.MaterializeIndirectEntryBlock(*meta);
  auto entry_block = DynamicCast<CompensationBasicBlock *>(cfg->EntryBlock());
  for (auto tool : ToolIterator(tools)) {
    tool->InstrumentEntryPoint(&factory, entry_block, kind, category);
  }
  factory.MaterializeRequestedBlocks();
  InstrumentControlFlow();
  InstrumentBlocks();
  InstrumentBlock();
  factory.RemoveUnreachableBlocks();
}

namespace {

// Try to finalize the control-flow by converting any remaining
// `DirectBasicBlock`s into `CachedBasicBlock`s (which are potentially preceded
// by `CompensationBasicBlock`.
static bool FinalizeControlFlow(BlockFactory *factory,
                                LocalControlFlowGraph *cfg) {
  for (auto block : cfg->Blocks()) {
    if (auto direct_block = DynamicCast<DirectBasicBlock *>(block)) {
      factory->RequestBlock(direct_block, kRequestBlockFromIndexOrCFGOnly);
    }
  }
  return factory->HasPendingMaterializationRequest();
}

}  // namespace

// Repeatedly apply LCFG-wide instrumentation for every tool, where tools are
// allowed to materialize direct basic blocks into other forms of basic
// blocks.
void BinaryInstrumenter::InstrumentControlFlow(void) {
  auto stop = false;
  for (auto num_iterations = 1; ; factory.MaterializeRequestedBlocks()) {
    for (auto tool : ToolIterator(tools)) {
      tool->InstrumentControlFlow(&factory, cfg);
    }
    if (stop) break;
    if (!factory.HasPendingMaterializationRequest()) {
      if (FinalizeControlFlow(&factory, cfg)) {
        factory.MaterializeRequestedBlocks();
        stop = true;
      } else {
        break;
      }
    } else if (++num_iterations >= FLAG_max_num_control_flow_iterations) {
      FinalizeControlFlow(&factory, cfg);
      stop = true;
    }
  }
}

// Apply LCFG-wide instrumentation for every tool.
void BinaryInstrumenter::InstrumentBlocks(void) {
  for (auto tool : ToolIterator(tools)) {
    tool->InstrumentBlocks(cfg);
  }
}

// Apply instrumentation to every block for every tool.
//
// Note: This applies tool-specific instrumentation for all tools to a single
//       block before moving on to the next block in the LCFG.
void BinaryInstrumenter::InstrumentBlock(void) {
  for (auto block : cfg->Blocks()) {
    for (auto tool : ToolIterator(tools)) {
      if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
        tool->InstrumentBlock(decoded_block);
      }
    }
  }
}

}  // namespace granary
