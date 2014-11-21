/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"

#include "granary/base/cstring.h"
#include "granary/base/pc.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"

#include "granary/context.h"
#include "granary/breakpoint.h"

namespace granary {

LocalControlFlowGraph::LocalControlFlowGraph(Context *context_)
    : context(context_),
      entry_block(nullptr),
      blocks(),
      first_new_block(nullptr),
      num_virtual_regs(512),
      num_basic_blocks(0),
      generation(0) {}

// Destroy the CFG and all basic blocks in the CFG.
LocalControlFlowGraph::~LocalControlFlowGraph(void) {
  for (BasicBlock *block(blocks.First()), *next(nullptr); block; block = next) {
    next = block->list.Next();
    delete block;
  }
}

// Return the entry basic block of this control-flow graph.
DecodedBasicBlock *LocalControlFlowGraph::EntryBlock(void) const {
  return DynamicCast<DecodedBasicBlock *>(entry_block);
}

// Returns an object that can be used inside of a range-based for loop.
BasicBlockIterator LocalControlFlowGraph::Blocks(void) const {
  return BasicBlockIterator(blocks.First());
}

// Returns an object that can be used inside of a range-based for loop.
ReverseBasicBlockIterator LocalControlFlowGraph::ReverseBlocks(void) const {
  return ReverseBasicBlockIterator(blocks.Last());
}

// Returns an object that can be used inside of a range-based for loop.
BasicBlockIterator LocalControlFlowGraph::NewBlocks(void) const {
  return BasicBlockIterator(first_new_block);
}

// Add a block to the CFG. If the block has successors that haven't yet been
// added, then add those too.
void LocalControlFlowGraph::AddBlock(BasicBlock *block) {
  if (block->list.IsLinked()) {
    GRANARY_ASSERT(-1 != block->Id());
  } else {
    // We might already have a block id if this block inherits the id of the
    // `DirectBasicBlock` that led to its materialization.
    if (-1 == block->id) block->id = num_basic_blocks++;

    // Distinguishes old from new blocks across iterations of
    // `InstrumentControlFlow`.
    block->generation = generation;
    blocks.Append(block);
    for (auto succ : block->Successors()) {  // Add the successors.
      AddBlock(succ.block);
    }
  }
}

// Add a block to the LCFG as the entry block.
void LocalControlFlowGraph::AddEntryBlock(BasicBlock *block) {
  entry_block = block;
  AddBlock(block);
  if (blocks.First() != block) {
    blocks.Remove(block);
    blocks.Prepend(block);
  }
  first_new_block = block;
}

// Allocate a new virtual register.
VirtualRegister LocalControlFlowGraph::AllocateVirtualRegister(int num_bytes) {
  GRANARY_ASSERT(0 < num_bytes && arch::GPR_WIDTH_BYTES >= num_bytes);
  GRANARY_ASSERT((1 << 16) > num_virtual_regs);
  return VirtualRegister(VR_KIND_VIRTUAL_GPR, static_cast<uint8_t>(num_bytes),
                         static_cast<uint16_t>(num_virtual_regs++));
}

}  // namespace granary
