/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/string.h"
#include "granary/breakpoint.h"
#include "granary/tool.h"

namespace granary {
namespace {

// Tools are added to the end of the list by storing a pointer to the last
// `next` pointer, or a pointer to `TOOLS`.
static Tool *TOOLS(nullptr);
static Tool **NEXT_TOOL(&TOOLS);

}  // namespace

// Register a tool with granary. Different instances of the same tool can be
// simultaneously registered, and a given instrumentation tool might register
// many distinct tool class instances.
void RegisterTool(const char *name, Tool *tool) {
  granary_break_on_fault_if(!tool);
  if (tool->is_registered) {
    return;
  }

  tool->name = name;
  tool->is_registered = true;

  *NEXT_TOOL = tool;
  NEXT_TOOL = &(tool->next);
}

// Returns the tool by name, or `nullptr` if the tool is not loaded.
Tool *FindTool(const char *name) {
  if (name) {
    for (auto tool : ToolIterator(TOOLS)) {
      if (StringsMatch(name, tool->name)) {
        return tool;
      }
    }
  }
  return nullptr;
}

// Initialize all loaded Granary tools.
void InitTools(InitKind kind) {
  for (auto tool(TOOLS); tool; tool = tool->next) {
    if (INIT_DYNAMIC == kind) {
      tool->InitDynamic();
    } else {
      tool->InitStatic();
    }
  }
}

// Dummy implementations of the tool API, so that tools don't need to define
// every API function.
Tool::Tool(void)
    : next(nullptr),
      is_registered(false),
      name(nullptr) {}

// Dummy implementation of InitDynamic for tools that can do all of their
// initialization
void Tool::InitDynamic(void) {}

void Tool::InitStatic(void) {
  granary_break_on_fault();
}

// Used to instrument control-flow instructions and decide how basic blocks
// should be materialized.
//
// This method is repeatedly executed until no more materialization
// requests are made.
void Tool::InstrumentControlFlow(BlockFactory *, LocalControlFlowGraph *) {}

// Used to implement more complex forms of instrumentation where tools need to
// see the entire local control-flow graph.
//
// This method is executed once per tool per instrumentation session.
void Tool::InstrumentBlocks(const LocalControlFlowGraph *) {}

// Used to implement the typical JIT-based model of single basic-block at a
// time instrumentation.
//
// This method is executed for each decoded BB in the local CFG,
// but is never re-executed for the same (tool, BB) pair in the current
// instrumentation session.
void Tool::InstrumentBlock(DecodedBasicBlock *) {}

// Returns an iterable of all registered tools.
ToolIterator Tools(void) {
  return ToolIterator(TOOLS);
}

}  // namespace granary
