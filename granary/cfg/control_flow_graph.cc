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
      next_new_block(nullptr),
      num_virtual_regs(512),
      num_basic_blocks(0) {}

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
    block->id = num_basic_blocks++;
    blocks.Append(block);
    if (!next_new_block) next_new_block = block;
  }
  for (auto succ : block->Successors()) {  // Add the successors.
    AddBlock(succ.block);
  }
}

// Add a block to the LCFG as the entry block.
void LocalControlFlowGraph::AddEntryBlock(BasicBlock *block) {
  entry_block = block;
  AddBlock(block);
  first_new_block = block;
  next_new_block = nullptr;
}

// Allocate a new virtual register.
VirtualRegister LocalControlFlowGraph::AllocateVirtualRegister(int num_bytes) {
  GRANARY_ASSERT(0 < num_bytes && arch::GPR_WIDTH_BYTES >= num_bytes);
  GRANARY_ASSERT((1 << 16) > num_virtual_regs);
  return VirtualRegister(VR_KIND_VIRTUAL_GPR, static_cast<uint8_t>(num_bytes),
                         static_cast<uint16_t>(num_virtual_regs++));
}

}  // namespace granary
