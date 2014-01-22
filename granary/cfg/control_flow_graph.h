/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_CFG_CONTROL_FLOW_GRAPH_H_
#define GRANARY_CFG_CONTROL_FLOW_GRAPH_H_

#include "granary/base/base.h"
#include "granary/base/types.h"

namespace granary {

// Forward declarations.
class BasicBlock;
class InFlightBasicBlock;
class FutureBasicBlock;
class ControlFlowGraph;
class BasicBlockMetaData;
class Environment;
class ControlFlowInstruction;

namespace detail {

class BasicBlockList;
class BasicBlockSuccessor;

// An iterator for basic blocks that implements C++11 range-based for loops.
class BasicBlockIterator {
 public:
  inline BasicBlockIterator begin(void) const {
    return *this;
  }

  inline BasicBlockIterator end(void) const {
    return BasicBlockIterator(nullptr);
  }

  inline bool operator!=(const BasicBlockIterator &that) const {
    return blocks != that.blocks;
  }

  void operator++(void);
  BasicBlock *operator*(void) const;

 private:
  friend class granary::ControlFlowGraph;

  BasicBlockIterator(void) = delete;
  inline explicit BasicBlockIterator(BasicBlockList *blocks_)
      : blocks(blocks_) {}

  // Pointer into a CFG's block list.
  GRANARY_POINTER(BasicBlockList) *blocks;
};

}  // namespace detail

// A control flow graph of basic blocks to instrument.
class ControlFlowGraph {
 public:
  GRANARY_INTERNAL_DEFINITION
  ControlFlowGraph(Environment *environment_, AppProgramCounter pc,
                   BasicBlockMetaData *meta=nullptr);

  ~ControlFlowGraph(void);

  // Convert a `FutureBasicBlock` into either of a:
  //    `CachedBasicBlock`:   If the block has already been translated.
  //    `InFlightBasicBlock`: If the block is already in the CFG. If not, a new
  //                          one might be made.
  //    `NativeBasicBlock`:   If the block jumps to somewhere that should go
  //                          native.
  void Materialize(const detail::BasicBlockSuccessor &target,
                   const BasicBlockMetaData *meta=nullptr);

  void Materialize(const ControlFlowInstruction *instruction,
                   const BasicBlockMetaData *meta=nullptr);

  // Returns an object that can be used inside of a range-based for loop. For
  // example:
  //
  //    for(auto block : cfg.Blocks())
  //      ...
  inline detail::BasicBlockIterator Blocks(void) {
    return detail::BasicBlockIterator(blocks);
  }

  GRANARY_POINTER(Environment) * const environment;

 private:

  GRANARY_INTERNAL_DEFINITION
  void Materialize(InFlightBasicBlock *block,
                   detail::BasicBlockList *block_list);

  // List of basic blocks known to this control-flow graph.
  detail::BasicBlockList *blocks;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ControlFlowGraph);
};

}  // namespace granary

#endif  // GRANARY_CFG_CONTROL_FLOW_GRAPH_H_
