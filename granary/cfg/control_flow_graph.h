/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_CFG_CONTROL_FLOW_GRAPH_H_
#define GRANARY_CFG_CONTROL_FLOW_GRAPH_H_

#include "granary/base/base.h"

namespace granary {

// Forward declarations.
class BasicBlock;
class BasicBlockIterator;
class FutureBasicBlock;
class InstrumentationPolicy;

namespace detail {
class BasicBlockList;
}  // namespace detail

class ControlFlowGraph {
 public:

  // Convert a `FutureBasicBlock` into either a `CachedBasicBlock` (if it has
  // shown up by now in the code cache) or into an `InFlightBasicBlock`.
  //
  // Note: This resets the `BasicBlockIterator` that was used to find this
  //       `FutureBasicBlock` (as it must have been found as a successor).
  void MaterializeBasicBlock(BasicBlockIterator &iterator,
                             const FutureBasicBlock *block,
                             InstrumentationPolicy *policy);

 private:
  detail::BasicBlockList *block_list_head;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ControlFlowGraph);
};

}  // namespace granary

#endif  // GRANARY_CFG_CONTROL_FLOW_GRAPH_H_
