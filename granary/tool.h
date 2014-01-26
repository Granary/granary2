/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_TOOL_H_
#define GRANARY_TOOL_H_

#include "granary/base/base.h"

namespace granary {

// Forward declarations.
class ControlFlowGraph;
class InFlightBasicBlock;
class Tool;

// Register a tool with granary. Different instances of the same tool can be
// simultaneously registered, and a given instrumentation tool might register
// many distinct tool class instances.
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
  virtual void InstrumentBB(InFlightBasicBlock *block);

 private:
  GRANARY_INTERNAL_DEFINITION friend void RegisterTool(Tool *tool);
  GRANARY_INTERNAL_DEFINITION friend void InitTools(InitKind kind);

  GRANARY_POINTER(Tool) *next;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Tool);
};

}  // namespace granary

#endif  // GRANARY_TOOL_H_
