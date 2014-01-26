/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/breakpoint.h"
#include "granary/tool.h"

namespace granary {
namespace {

static Tool *TOOLS(nullptr);

}  // namespace

// Register a tool with granary. Different instances of the same tool can be
// simultaneously registered, and a given instrumentation tool might register
// many distinct tool class instances.
void RegisterTool(Tool *tool) {
  granary_break_on_fault_if(!tool);
  tool->next = TOOLS;
  TOOLS = tool;
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
    : next(nullptr) {}

// Dummy implementation of InitDynamic for tools that can do all of their
// initialization
void Tool::InitDynamic(void) {}

void Tool::InitStatic(void) {
  granary_break_on_fault();
}

void Tool::InstrumentCFG(ControlFlowGraph *) {}

void Tool::InstrumentBB(InFlightBasicBlock *) {}

}  // namespace granary
