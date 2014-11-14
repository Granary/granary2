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

LocalControlFlowGraph::LocalControlFlowGraph(ContextInterface *context_)
    : context(context_),
      entry_block(nullptr),
      first_block(nullptr),
      last_block(nullptr),
      first_new_block(nullptr),
      next_new_block(nullptr),
      num_virtual_regs(512),
      num_basic_blocks(0) {}

// Destroy the CFG and all basic blocks in the CFG.
LocalControlFlowGraph::~LocalControlFlowGraph(void) {
  for (BasicBlock *block(first_block), *next(nullptr); block; block = next) {
    next = block->list.GetNext(block);
    delete block;
  }
  memset(this, 0, sizeof *this);
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

  if (GRANARY_UNLIKELY(!first_block)) {
    GRANARY_ASSERT(IsA<DecodedBasicBlock *>(block) ||
                   IsA<CachedBasicBlock *>(block));
    first_block = block;
    // We assume that the first added block is one of a `DecodedBasicBlock`
    // (and by extension a `CompensationBasicBlock`) or a `CachedBasicBlock`.
    if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(block)) {
      entry_block = decoded_block;
    }
  } else {
    last_block->list.SetNext(last_block, block);
  }

  last_block = block;

  if (!next_new_block) {
    next_new_block = block;
  }

  for (auto succ : block->Successors()) {  // Add the successors.
    AddBlock(succ.block);
  }
}

// Allocate a new virtual register.
VirtualRegister LocalControlFlowGraph::AllocateVirtualRegister(int num_bytes) {
  GRANARY_ASSERT(0 < num_bytes && arch::GPR_WIDTH_BYTES >= num_bytes);
  GRANARY_ASSERT((1 << 16) > num_virtual_regs);
  return VirtualRegister(VR_KIND_VIRTUAL_GPR, static_cast<uint8_t>(num_bytes),
                         static_cast<uint16_t>(num_virtual_regs++));
}

}  // namespace granary
