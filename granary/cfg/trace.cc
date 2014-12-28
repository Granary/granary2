/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"

#include "granary/base/cstring.h"
#include "granary/base/pc.h"

#include "granary/cfg/block.h"
#include "granary/cfg/trace.h"

#include "granary/context.h"
#include "granary/breakpoint.h"

namespace granary {

Trace::Trace(Context *context_)
    : context(context_),
      entry_block(nullptr),
      blocks(),
      first_new_block(nullptr),
      num_virtual_regs(512),
      num_basic_blocks(0),
      generation(0) {}

// Destroy the CFG and all basic blocks in the CFG.
Trace::~Trace(void) {
  for (Block *block(blocks.First()), *next(nullptr); block; block = next) {
    next = block->list.Next();
    delete block;
  }
}

// Return the entry basic block of this control-flow graph.
DecodedBlock *Trace::EntryBlock(void) const {
  return DynamicCast<DecodedBlock *>(entry_block);
}

// Returns an object that can be used inside of a range-based for loop.
BlockIterator Trace::Blocks(void) const {
  return BlockIterator(blocks.First());
}

// Returns an object that can be used inside of a range-based for loop.
ReverseBlockIterator Trace::ReverseBlocks(void) const {
  return ReverseBlockIterator(blocks.Last());
}

// Returns an object that can be used inside of a range-based for loop.
BlockIterator Trace::NewBlocks(void) const {
  return BlockIterator(first_new_block);
}

// Add a block to the CFG. If the block has successors that haven't yet been
// added, then add those too.
void Trace::AddBlock(Block *block) {
  if (block->list.IsLinked()) {
    GRANARY_ASSERT(-1 != block->Id());
  } else {
    // We might already have a block id if this block inherits the id of the
    // `DirectBlock` that led to its materialization.
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

// Add a block to the trace as the entry block.
void Trace::AddEntryBlock(Block *block) {
  entry_block = block;
  AddBlock(block);
  if (blocks.First() != block) {
    blocks.Remove(block);
    blocks.Prepend(block);
  }
  first_new_block = block;
  ++generation;
}

// Allocate a new virtual register.
VirtualRegister Trace::AllocateVirtualRegister(
    size_t num_bytes) {
  GRANARY_ASSERT(0 < num_bytes && arch::GPR_WIDTH_BYTES >= num_bytes);
  GRANARY_ASSERT((1 << 16) > num_virtual_regs);
  return VirtualRegister(kVirtualRegisterKindVirtualGpr,
                         static_cast<uint8_t>(num_bytes),
                         static_cast<uint16_t>(num_virtual_regs++));
}

}  // namespace granary
