/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_TOOL_H_
#define GRANARY_TOOL_H_

#include "granary/base/base.h"
#include "granary/base/list.h"
#include "granary/init.h"

namespace granary {

// Forward declarations.
class BlockFactory;
class LocalControlFlowGraph;
class DecodedBasicBlock;
class Tool;

// Register a tool with granary. Different instances of the same tool can be
// simultaneously registered, and a given instrumentation tool might register
// many distinct tool class instances.
//
// TODO(pag): Need a mechanism to register multiple available concurrent
//            versions of a tool to be run.
void RegisterTool(const char *name, Tool *tool);

// Returns the tool by name, or `nullptr` if the tool is not loaded.
Tool *FindTool(const char *name);

#ifdef GRANARY_INTERNAL

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

  // Used to instrument control-flow instructions and decide how basic blocks
  // should be materialized.
  //
  // This method is repeatedly executed until no more materialization
  // requests are made.
  virtual void InstrumentControlFlow(BlockFactory *materializer,
                                     LocalControlFlowGraph *cfg);

  // Used to implement more complex forms of instrumentation where tools need to
  // see the entire local control-flow graph.
  //
  // This method is executed once per tool per instrumentation session.
  virtual void InstrumentBlocks(const LocalControlFlowGraph *cfg);

  // Used to implement the typical JIT-based model of single basic-block at a
  // time instrumentation.
  //
  // This method is executed for each decoded BB in the local CFG,
  // but is never re-executed for the same (tool, BB) pair in the current
  // instrumentation session.
  virtual void InstrumentBlock(DecodedBasicBlock *block);

 GRANARY_PUBLIC:

  GRANARY_POINTER(Tool) * GRANARY_CONST next;
  GRANARY_CONST bool is_registered;
  const char * GRANARY_CONST name;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Tool);
};

#ifdef GRANARY_INTERNAL

typedef LinkedListIterator<Tool> ToolIterator;

// Returns an iterable of all registered tools.
ToolIterator Tools(void);

#endif  // GRANARY_INTERNAL

}  // namespace granary

#endif  // GRANARY_TOOL_H_
