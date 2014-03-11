/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;

// Simple tool for tracing direct and synthesized jumps (but not conditional
// jumps) in a control-flow graph.
class JumpFollower : public Tool {
 public:
  virtual ~JumpFollower(void) = default;
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     LocalControlFlowGraph *cfg) {
    for (auto block : cfg->NewBlocks()) {
      if (IsA<IndirectBasicBlock *>(block) || IsA<ReturnBasicBlock *>(block)) {
        continue;
      }

      for (auto succ : block->Successors()) {
        if (succ.cti->IsConditionalJump()) {
          // Expand the target of a conditional jump only if it's a back-edge.
          if (succ.block->StartAppPC() < block->StartAppPC()) {
            factory->RequestBlock(succ.block);
            break;
          }

        // If we haven't already expanded a conditional jump, or there was no
        // conditional jump, then expand the direct jump.
        } else if (succ.cti->IsJump() && !succ.cti->HasIndirectTarget()) {
          factory->RequestBlock(succ.block);
          break;
        }
      }
    }
  }
};

// Initialize the `follow_jumps` tool.
GRANARY_CLIENT_INIT({
  RegisterTool<JumpFollower>("follow_jumps");
})
