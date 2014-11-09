/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary.h>

using namespace granary;

// Simple tool for tracing direct control-flow instructions into traces.
class JumpFollower : public InstrumentationTool {
 public:
  virtual ~JumpFollower(void) = default;
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      if (!IsA<DecodedBasicBlock *>(block)) {
        continue;
      }
      for (auto succ : block->Successors()) {
        if (succ.cfi->IsConditionalJump()) {
          // Expand the target of a conditional jump only if it's a back-edge.
          // The default branch prediction behavior on x86, absent any prior
          // knowledge, is to predict a conditional jump if it's a back-edge.
          // We expect that compilers will attempt to layout code such that
          // this is the expected path to follow.
          if (succ.block->StartAppPC() < block->StartAppPC()) {
            factory->RequestBlock(succ.block);
            break;
          }

        // If we haven't already expanded a conditional jump, or there was no
        // conditional jump, then expand the direct jump.
        } else if (succ.cfi->IsJump() && !succ.cfi->HasIndirectTarget()) {
          factory->RequestBlock(succ.block);
          break;
        }
      }
    }
  }
};

// Initialize the `follow_jumps` tool.
GRANARY_ON_CLIENT_INIT() {
  RegisterInstrumentationTool<JumpFollower>("follow_jumps");
}
