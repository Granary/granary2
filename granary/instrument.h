/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_INSTRUMENT_H_
#define GRANARY_INSTRUMENT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/factory.h"

#include "granary/entry.h"

namespace granary {

// Forward declarations.
class BlockMetaData;
class ContextInterface;
class InstrumentationTool;

// Instrument some initial code (described by `meta`) and fills a LCFG `cfg`
// with the instrumented code. `meta` is taken as being "owned", i.e. no one
// should be concurrently modifying `meta`!
//
// Note: `meta` might be deleted if some block with the same meta-data already
//       exists in the code cache index. Therefore, one must use the returned
//       meta-data hereafter.
class BinaryInstrumenter {
 public:
  BinaryInstrumenter(ContextInterface *context_, LocalControlFlowGraph *cfg_,
                     BlockMetaData **meta_);
  ~BinaryInstrumenter(void);

  // Instrument some code as-if it is targeted by a direct CFI.
  void InstrumentDirect(void);

  // Instrument some code as-if it is targeted by an indirect CFI.
  void InstrumentIndirect(void);

  // Instrument some code as-if it is targeted by a native entrypoint. These
  // are treated as being the initial points of instrumentation.
  void InstrumentEntryPoint(EntryPointKind kind, int category);

 private:
  // Repeatedly apply LCFG-wide instrumentation for every tool, where tools are
  // allowed to materialize direct basic blocks into other forms of basic
  // blocks.
  void InstrumentControlFlow(void);

  // Apply LCFG-wide instrumentation for every tool.
  void InstrumentBlocks(void);

  // Apply instrumentation to every block for every tool.
  //
  // Note: This applies tool-specific instrumentation for all tools to a single
  //       block before moving on to the next block in the LCFG.
  void InstrumentBlock(void);

  ContextInterface *context;
  InstrumentationTool *tools;
  BlockMetaData **meta;

  LocalControlFlowGraph *cfg;
  BlockFactory factory;
};

}  // namespace granary

#endif  // GRANARY_INSTRUMENT_H_
