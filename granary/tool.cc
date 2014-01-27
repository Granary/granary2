/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/breakpoint.h"
#include "granary/tool.h"

namespace granary {
namespace {

static Tool *TOOLS(nullptr);
static Tool **NEXT_TOOL(&TOOLS);

}  // namespace

// Register a tool with granary. Different instances of the same tool can be
// simultaneously registered, and a given instrumentation tool might register
// many distinct tool class instances.
void RegisterTool(Tool *tool) {
  granary_break_on_fault_if(!tool);
  if (tool->is_registered) {
    return;
  }

  tool->is_registered = true;
  *NEXT_TOOL = tool;
  NEXT_TOOL = &(tool->next);
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
void Tool::InstrumentCFG(ControlFlowGraph *) {}

// Used to initialize an instrumentation session.
void Tool::BeginInstrumentBB(ControlFlowGraph *) {}
void Tool::InstrumentBB(InFlightBasicBlock *) {}
void Tool::EndInstrumentBB(ControlFlowGraph *) {}

// Returns an iterable of all registered tools.
LinkedListIterator<Tool> Tools(void) {
  return LinkedListIterator<Tool>(TOOLS);
}

}  // namespace granary
