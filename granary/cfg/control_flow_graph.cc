/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/arch/base.h"
#include "granary/base/list.h"
#include "granary/base/new.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"

namespace granary {
namespace detail {

// Defines a list of a basic blocks within a control-flow graph.
class BasicBlockList {
 public:
  ListHead list;
  std::unique_ptr<BasicBlock> block;

  explicit BasicBlockList(BasicBlock *block_)
      : block(block_) {}

  ~BasicBlockList(void) = default;

  // Basic block lists are allocated from a global memory pool using the
  // `new` and `delete` operators.
  GRANARY_DEFINE_NEW_ALLOCATOR(BasicBlockList, {
    SHARED = true,
    ALIGNMENT = 16
  });

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(BasicBlockList);
};

// Used to check to make sure an active iterator is not equivalent to the end
// iterator.
bool BasicBlockIterator::operator!=(const BasicBlockIterator &that) const {
  return blocks != that.blocks;
}

// Move the iterator to the next basic block.
const BasicBlockIterator &BasicBlockIterator::operator++(void) {
  blocks = blocks->list.GetNext(blocks);
  return *this;
}

// Get a basic block out of the iterator.
BasicBlock *BasicBlockIterator::operator*(void) {
  return blocks->block.get();
}

}  // namespace detail

// Initialize a CFG starting with an in-flight basic block as the entrypoint.
ControlFlowGraph::ControlFlowGraph(InFlightBasicBlock *first_block)
    : blocks(new detail::BasicBlockList(first_block)) {}

// Destroy the CFG.
ControlFlowGraph::~ControlFlowGraph(void) {
  for (detail::BasicBlockList *curr(blocks), *next(nullptr); curr; curr = next) {
    next = curr->list.GetNext(curr);
    delete curr;
  }
  blocks = nullptr;
}

}  // namespace granary
