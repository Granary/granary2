/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"

#include "granary/base/pc.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"

#include "granary/context.h"
#include "granary/breakpoint.h"

namespace granary {

// Destroy the CFG and all basic blocks in the CFG.
LocalControlFlowGraph::~LocalControlFlowGraph(void) {
  // Start by marking every block as owned; we're destroying them anyway so
  // this sets up a simple invariant regarding the interaction between freeing
  // instructions and basic blocks.
  for (auto block : BasicBlockIterator(first_block)) {
    block->Acquire();
  }

  // Free up all of the instruction lists.
  for (auto block : BasicBlockIterator(first_block)) {
    auto decoded_block = DynamicCast<DecodedBasicBlock *>(block);
    if (decoded_block) {
      decoded_block->FreeInstructionList();
    }
  }

  // Free up all the basic blocks.
  for (BasicBlock *curr(first_block), *next(nullptr); curr; curr = next) {
    next = curr->list.GetNext(curr);
    delete curr;
  }

  context = nullptr;
  entry_block = nullptr;
  first_block = nullptr;
  last_block = nullptr;
  first_new_block = nullptr;
}

// Return the entry basic block of this control-flow graph.
DecodedBasicBlock *LocalControlFlowGraph::EntryBlock(void) const {
  return entry_block;
}

// Returns an object that can be used inside of a range-based for loop.
BasicBlockIterator LocalControlFlowGraph::Blocks(void) const {
  return BasicBlockIterator(first_block);
}

// Returns an object that can be used inside of a range-based for loop.
ReverseBasicBlockIterator LocalControlFlowGraph::ReverseBlocks(void) const {
  return ReverseBasicBlockIterator(last_block);
}

// Returns an object that can be used inside of a range-based for loop.
BasicBlockIterator LocalControlFlowGraph::NewBlocks(void) const {
  return BasicBlockIterator(first_new_block);
}

// Add a block to the CFG. If the block has successors that haven't yet been
// added, then add those too.
void LocalControlFlowGraph::AddBlock(BasicBlock *block) {
  if (block->list.IsAttached()) {
    GRANARY_ASSERT(-1 != block->Id());
    return;  // Already in the CFG.
  }
  block->id = num_basic_blocks++;

  if (!first_block) {
    first_block = block;

    // We assume that the first added block is one of a `DecodedBasicBlock`
    // (and by extension a `CompensationBasicBlock`) or a `CachedBasicBlock`.
    if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
      entry_block = decoded_block;
    }

    // The control-flow graph has sole ownership over the entry basic block.
    // All other basic blocks are owned by control-transfer instructions.
    block->MarkAsPermanent();
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

// Allocate a new virtual register.
VirtualRegister LocalControlFlowGraph::AllocateVirtualRegister(int num_bytes) {
  GRANARY_ASSERT(0 < num_bytes && arch::GPR_WIDTH_BYTES >= num_bytes);
  GRANARY_ASSERT((1 << 16) > num_virtual_regs);
  return VirtualRegister(VR_KIND_VIRTUAL, static_cast<uint8_t>(num_bytes),
                         static_cast<uint16_t>(num_virtual_regs++));
}

}  // namespace granary
