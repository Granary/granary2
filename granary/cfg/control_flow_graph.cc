/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"
#include "granary/base/types.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"

namespace granary {

// Destroy the CFG and all basic blocks in the CFG.
LocalControlFlowGraph::~LocalControlFlowGraph(void) {

  // Start by marking every block as owned; we're destroying them anyway so
  // this sets up a simple invariant regarding the interaction between freeing
  // instructions and basic blocks.
  for (BasicBlock *curr(first_block); curr; curr = curr->list.GetNext(curr)) {
    curr->Acquire();
  }

  // Free up all of the instruction lists.
  for (BasicBlock *curr(first_block); curr; curr = curr->list.GetNext(curr)) {
    auto decoded_block = DynamicCast<DecodedBasicBlock *>(curr);
    if (decoded_block) {
      decoded_block->FreeInstructionList();
    }
  }

  // Free up all the basic blocks.
  for (BasicBlock *curr(first_block), *next(nullptr); curr; curr = next) {
    next = curr->list.GetNext(curr);
    delete curr;
  }

  first_block = nullptr;
  last_block = nullptr;
  first_new_block = nullptr;
}

// Return the entry basic block of this control-flow graph.
DecodedBasicBlock *LocalControlFlowGraph::EntryBlock(void) const {
  return first_block;
}

// Returns an object that can be used inside of a range-based for loop.
BasicBlockIterator LocalControlFlowGraph::Blocks(void) const {
  return BasicBlockIterator(first_block);
}

// Returns an object that can be used inside of a range-based for loop.
BasicBlockIterator LocalControlFlowGraph::NewBlocks(void) const {
  return BasicBlockIterator(first_new_block);
}

// Add a block to the CFG. If the block has successors that haven't yet been
// added, then add those too.
void LocalControlFlowGraph::AddBlock(BasicBlock *block) {
  if (block->list.IsAttached()) {
    return;  // Already in the CFG.
  }

  // The control-flow graph has sole ownership over the initial basic block.
  // All other basic blocks are owned by control-transfer instructions.
  if (!first_block) {
    auto decoded_block = DynamicCast<DecodedBasicBlock *>(block);
    granary_break_on_fault_if(!decoded_block);
    block->MarkAsPermanent();
    first_block = decoded_block;
  } else {
    last_block->list.SetNext(last_block, block);
  }

  last_block = block;

  if (!first_new_block) {
    first_new_block = block;
  }

  for (auto succ : block->Successors()) {  // Add the successors.
    AddBlock(succ.block);
  }
}

}  // namespace granary
