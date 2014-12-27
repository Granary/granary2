/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_TRACE_H_
#define GRANARY_CFG_TRACE_H_

#ifdef GRANARY_INTERNAL
# include "arch/base.h"
# include "granary/code/register.h"
#endif

#include "granary/base/base.h"
#include "granary/base/list.h"
#include "granary/base/pc.h"
#include "granary/cfg/iterator.h"

namespace granary {

// Forward declarations.
class Block;
class BlockMetaData;
class DecodedBlock;
class Trace;
class BlockFactory;
GRANARY_INTERNAL_DEFINITION class DirectEdge;
GRANARY_INTERNAL_DEFINITION class IndirectEdge;
GRANARY_INTERNAL_DEFINITION class Context;

// A control flow graph of basic blocks to instrument.
class Trace final {
 public:
  GRANARY_INTERNAL_DEFINITION
  explicit Trace(Context *context_);

  // Destroy the CFG and all basic blocks in the CFG.
  ~Trace(void);

  // Return the entry basic block of this control-flow graph.
  DecodedBlock *EntryBlock(void) const;

  // Returns an iterable that can be used inside of a range-based for loop. For
  // example:
  //
  //    for (auto block : cfg.Blocks())
  //      ...
  BlockIterator Blocks(void) const;

  // Returns an iterable that can be used inside of a range-based for loop. For
    // example:
    //
    //    for (auto block : cfg.Blocks())
    //      ...
  ReverseBlockIterator ReverseBlocks(void) const;

  // Returns an iterable that can be used inside of a range-based for loop. For
  // example:
  //
  //    for (auto block : cfg.NewBlocks())
  //      ...
  //
  // The distinction between `Blocks` and `NewBlocks` is relevant to
  // block materialization passes, where `Blocks` is the list of all basic
  // blocks, and `NewBlocks` is the list of newly materialized basic blocks.
  BlockIterator NewBlocks(void) const;

  // Add a block to the CFG. If the block has successors that haven't yet been
  // added, then add those too.
  GRANARY_INTERNAL_DEFINITION void AddBlock(Block *block);
  GRANARY_INTERNAL_DEFINITION void AddEntryBlock(Block *block);

  // Allocate a new virtual register.
  VirtualRegister AllocateVirtualRegister(
      size_t num_bytes=arch::GPR_WIDTH_BYTES);

 private:
  friend class BlockFactory;  // For `first_new_block`.

  Trace(void) = delete;

  // Context to which this trace belongs. This is needed so that we can allocate
  // edge code data structures.
  GRANARY_INTERNAL_DEFINITION Context *context;

  // List of basic blocks known to this control-flow graph.
  GRANARY_INTERNAL_DEFINITION Block *entry_block;
  GRANARY_INTERNAL_DEFINITION ListOfListHead<Block> blocks;
  GRANARY_INTERNAL_DEFINITION Block *first_new_block;

  // Counter of how many virtual registers were allocated within this trace.
  //
  // We default this to a fairly large number so that virtual register numbers
  // never conflict with actual register numbers. This is convenient for
  // virtual register save/restore slots.
  GRANARY_INTERNAL_DEFINITION int num_virtual_regs;

  // Counter of how many basic blocks were added to this trace. This does not
  // necessarily track the exact number of blocks present at any one time.
  GRANARY_INTERNAL_DEFINITION int num_basic_blocks;

  // Current block generation counter.
  GRANARY_INTERNAL_DEFINITION int generation;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Trace);
};

}  // namespace granary

#endif  // GRANARY_CFG_TRACE_H_
