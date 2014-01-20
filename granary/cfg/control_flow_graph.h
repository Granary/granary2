/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_CFG_CONTROL_FLOW_GRAPH_H_
#define GRANARY_CFG_CONTROL_FLOW_GRAPH_H_

#include "granary/base/base.h"

namespace granary {

// Forward declarations.
class BasicBlock;
class InFlightBasicBlock;
class FutureBasicBlock;
class ControlFlowGraph;

namespace detail {

class BasicBlockList;
class BasicBlockFinder;

// An iterator for basic blocks that implements C++11 range-based for loops.
class BasicBlockIterator {
 public:
  inline ~BasicBlockIterator(void) { blocks = nullptr; }

  bool operator!=(const BasicBlockIterator &) const;
  const BasicBlockIterator &operator++(void);
  BasicBlock *operator*(void);

 private:
  friend class BasicBlockFinder;

  BasicBlockIterator(void) = delete;
  inline explicit BasicBlockIterator(BasicBlockList *blocks_)
      : blocks(blocks_) {}

  // Pointer into a CFG's block list.
  BasicBlockList *blocks;
};

// A container that is used by range based for loops for getting basic block
// iterators from a control-flow graph.
class BasicBlockFinder {
 public:
  inline BasicBlockIterator begin(void) { return BasicBlockIterator(blocks); }
  inline BasicBlockIterator end(void) const {
    return BasicBlockIterator(nullptr);
  }

 private:
  friend class granary::ControlFlowGraph;

  BasicBlockFinder(void) = delete;
  inline explicit BasicBlockFinder(BasicBlockList *blocks_)
      : blocks(blocks_) {}

  // Pointer into a CFG's block list.
  BasicBlockList *blocks;
};

}  // namespace detail

// A control flow graph of basic blocks to instrument.
class ControlFlowGraph {
 public:
  // Initialize a CFG starting with an in-flight basic block as the entrypoint.
  explicit ControlFlowGraph(InFlightBasicBlock *first_block);

  ~ControlFlowGraph(void);

  // Convert a `FutureBasicBlock` into either of a:
  //    `CachedBasicBlock`:   If the block has already been translated.
  //    `InFlightBasicBlock`: If the block is already in the CFG. If not, a new
  //                          one might be made.
  //    `NativeBasicBlock`:   If the block jumps to somewhere that should go
  //                          native.
  //
  // Note: This resets the `BasicBlockIterator` that was used to find this
  //       `FutureBasicBlock` (as it must have been found as a successor).
  void MaterializeBasicBlock(const FutureBasicBlock *block);

  // Returns an object that can be used inside of a range-based for loop. For
  // example:
  //
  //    for(auto block : cfg.Blocks())
  //      ...
  inline detail::BasicBlockFinder Blocks(void) {
    return detail::BasicBlockFinder(blocks);
  }

 private:
  ControlFlowGraph(void) = delete;

  detail::BasicBlockList *blocks;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ControlFlowGraph);
};

}  // namespace granary

#endif  // GRANARY_CFG_CONTROL_FLOW_GRAPH_H_
