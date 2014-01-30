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
    if (InitKind::DYNAMIC == kind) {
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
      is_registered(false) {}

// Dummy implementation of InitDynamic for tools that can do all of their
// initialization
void Tool::InitDynamic(void) {}

void Tool::InitStatic(void) {
  granary_break_on_fault();
}
void Tool::InstrumentCFG(LocalControlFlowGraph *) {}

// Used to initialize an instrumentation session.
void Tool::BeginInstrumentBB(LocalControlFlowGraph *) {}
void Tool::InstrumentBB(InFlightBasicBlock *) {}
void Tool::EndInstrumentBB(LocalControlFlowGraph *) {}

// Returns an iterable of all registered tools.
ToolIterator Tools(void) {
  return ToolIterator(TOOLS);
}

}  // namespace granary
