/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_CONTROL_FLOW_GRAPH_H_
#define GRANARY_CFG_CONTROL_FLOW_GRAPH_H_

#ifdef GRANARY_INTERNAL
# include "granary/arch/base.h"
# include "granary/code/register.h"
#endif

#include "granary/base/base.h"
#include "granary/base/pc.h"
#include "granary/cfg/iterator.h"

namespace granary {

// Forward declarations.
class BasicBlock;
class BlockMetaData;
class DecodedBasicBlock;
class LocalControlFlowGraph;
class BlockFactory;
GRANARY_INTERNAL_DEFINITION class DirectEdge;
GRANARY_INTERNAL_DEFINITION class ContextInterface;

// A control flow graph of basic blocks to instrument.
class LocalControlFlowGraph final {
 public:
  GRANARY_INTERNAL_DEFINITION
  inline explicit LocalControlFlowGraph(ContextInterface *context_)
      : context(context_),
        first_block(nullptr),
        last_block(nullptr),
        first_new_block(nullptr),
        num_virtual_regs(0),
        num_basic_blocks(0) {}

  // Destroy the CFG and all basic blocks in the CFG.
  ~LocalControlFlowGraph(void);

  // Return the entry basic block of this control-flow graph.
  DecodedBasicBlock *EntryBlock(void) const;

  // Returns an iterable that can be used inside of a range-based for loop. For
  // example:
  //
  //    for (auto block : cfg.Blocks())
  //      ...
  BasicBlockIterator Blocks(void) const;

  // Returns an iterable that can be used inside of a range-based for loop. For
  // example:
  //
  //    for (auto block : cfg.NewBlocks())
  //      ...
  //
  // The distinction between `Blocks` and `NewBlocks` is relevant to
  // block materialization passes, where `Blocks` is the list of all basic
  // blocks, and `NewBlocks` is the list of newly materialized basic blocks.
  BasicBlockIterator NewBlocks(void) const;

  // Add a block to the CFG. If the block has successors that haven't yet been
  // added, then add those too.
  GRANARY_INTERNAL_DEFINITION void AddBlock(BasicBlock *block);

  // Allocate a new virtual register.
  GRANARY_INTERNAL_DEFINITION
  VirtualRegister AllocateVirtualRegister(int num_bytes);

  // Allocate a direct edge structure. This uses this LCFG's context to
  // allocate the direct edge and it's associated code.
  GRANARY_INTERNAL_DEFINITION
  DirectEdge *AllocateDirectEdge(const BlockMetaData *source_meta,
                                 BlockMetaData *dest_meta);

 private:
  friend class BlockFactory;  // For `first_new_block`.

  LocalControlFlowGraph(void) = delete;

  // Context to which this LCFG belongs. This is needed so that we can allocate
  // edge code data structures.
  GRANARY_INTERNAL_DEFINITION ContextInterface *context;

  // List of basic blocks known to this control-flow graph.
  GRANARY_INTERNAL_DEFINITION DecodedBasicBlock *first_block;
  GRANARY_INTERNAL_DEFINITION BasicBlock *last_block;
  GRANARY_INTERNAL_DEFINITION BasicBlock *first_new_block;

  // Counter of how many virtual registers were allocated within this LCFG.
  GRANARY_INTERNAL_DEFINITION int num_virtual_regs;

  // Counter of how many basic blocks were added to this LCFG. This does not
  // necessarily track the exact number of blocks present at any one time.
  GRANARY_INTERNAL_DEFINITION int num_basic_blocks;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(LocalControlFlowGraph);
};

}  // namespace granary

#endif  // GRANARY_CFG_CONTROL_FLOW_GRAPH_H_
