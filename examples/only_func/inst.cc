/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;

// Simple tool for (almost) ensuring that only one function is ever
// instrumented. The way this tool works is that it prevents Granary from
// instrumenting the targets of function calls.
//
// This tool doesn't handle cases like:
//    1) Returns specialized by another tool.
//    2) Tail-calls to other functions through direct/indirect jumps.
class OnlyFunctionDecoder : public Tool {
 public:
  virtual ~OnlyFunctionDecoder(void) = default;
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      for (auto succ : block->Successors()) {
        if (succ.cfi->IsFunctionCall()) {
          factory->RequestBlock(succ.block, BlockRequestKind::REQUEST_NATIVE);
        }
      }
    }
  }
};

// Initialize the `whole_func` tool.
GRANARY_CLIENT_INIT({
  RegisterTool<OnlyFunctionDecoder>("only_func");
})
