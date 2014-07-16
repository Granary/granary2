/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;

// Simple tool decoding all blocks in a function.
class WholeFunctionDecoder : public Tool {
 public:
  virtual ~WholeFunctionDecoder(void) = default;
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      for (auto succ : block->Successors()) {
        if (!succ.cfi->IsFunctionCall()) {
          factory->RequestBlock(succ.block);
        }
      }
    }
  }
};

// Initialize the `whole_func` tool.
GRANARY_CLIENT_INIT({
  RegisterTool<WholeFunctionDecoder>("whole_func");
})
