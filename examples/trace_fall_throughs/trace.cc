/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;

// Simple tool for tracing direct and synthesized jumps (but not conditional
// jumps) in a control-flow graph.
class TraceFallThroughCTIs : public Tool {
 public:
  virtual ~TraceFallThroughCTIs(void) = default;
  virtual void InstrumentCFG(ControlFlowGraph *cfg) {
    for (auto block : cfg->Blocks()) {
      if (IsA<UnknownBasicBlock *>(block)) {
        continue;
      }
      for (auto succ : block->Successors()) {
        if (succ.cti->IsJump() &&
            !succ.cti->IsConditionalJump() &&
            !succ.cti->HasIndirectTarget()) {
          cfg->Materialize(succ);
        }
      }
    }
  }
} static TRACER;

// Initialize the `trace_fall_throughs` tool.
GRANARY_INIT(trace_fall_throughs, {
  RegisterTool(&TRACER);
})
