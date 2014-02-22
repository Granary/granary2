/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;

// Simple tool for tracing memory loads and stores.
class MemTracer : public Tool {
 public:
  virtual ~MemTracer(void) = default;

  void InstrumentMemOp(DecodedBasicBlock *bb, MemoryOperand mloc) {
    auto addr = GetEffectiveAddress(bb, mloc);

    if (mloc.IsReadWrite()) {

    } else if (mloc.IsWrite()) {  // Write-only.

    } else {  // Read-only.

    }

    GRANARY_UNUSED(addr);
  }

  // Instrument a basic block.
  virtual void InstrumentBlock(DecodedBasicBlock *bb) {
    MemoryOperand mloc1;
    MemoryOperand mloc2;
    for (auto instr : bb->AppInstructions()) {
      auto num_matched = instr->CountMatchedOperands(ReadOrWriteTo(mloc1),
                                                     ReadOrWriteTo(mloc2));
      if (2 == num_matched) {
        InstrumentMemOp(bb, mloc1);
        InstrumentMemOp(bb, mloc2);
      } else if (1 == num_matched) {
        InstrumentMemOp(bb, mloc1);
      }
    }
  }
} static TRACER;

// Initialize the `trace_mem` tool.
GRANARY_INIT({
  RegisterTool("trace_mem", &TRACER);
})
