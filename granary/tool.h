/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_TOOL_H_
#define GRANARY_TOOL_H_

#include "granary/base/base.h"
#include "granary/base/list.h"

namespace granary {

// Forward declarations.
class ControlFlowGraph;
class InFlightBasicBlock;
class Tool;

// Register a tool with granary. Different instances of the same tool can be
// simultaneously registered, and a given instrumentation tool might register
// many distinct tool class instances.
//
// TODO(pag): Need a mechanism to register multiple available concurrent
//            versions of a tool to be run.
void RegisterTool(Tool *tool);

#ifdef GRANARY_INTERNAL

enum InitKind {
  DYNAMIC,
  STATIC
};

// Initialize all loaded Granary tools.
void InitTools(InitKind kind);

#endif  // GRANARY_INTERNAL

// Describes the structure of tools.
class Tool {
 public:
  Tool(void);
  virtual ~Tool(void) = default;

  // Used to distinguish between static and dynamic instrumentation modes.
  virtual void InitDynamic(void);
  virtual void InitStatic(void);


  // Used to implement more complex forms of instrumentation where tools can
  // tell Granary how to expand a control-flow graph, what basic blocks should
  // be instrumented and not instrumented, and as a mechanism to determine
  // if control branches to an already cached basic block.
  virtual void InstrumentCFG(ControlFlowGraph *cfg);

  // Used to implement the typical JIT-based model of single basic-block at a
  // time instrumentation.
  virtual void BeginInstrumentBB(ControlFlowGraph *cfg);
  virtual void InstrumentBB(InFlightBasicBlock *block);
  virtual void EndInstrumentBB(ControlFlowGraph *cfg);

 GRANARY_PUBLIC:
  GRANARY_POINTER(Tool) * GRANARY_CONST next;
  GRANARY_CONST bool is_registered;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Tool);
};

#ifdef GRANARY_INTERNAL

// Returns an iterable of all registered tools.
LinkedListIterator<Tool> Tools(void);

#endif  // GRANARY_INTERNAL

}  // namespace granary

#endif  // GRANARY_TOOL_H_
