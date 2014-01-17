/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_CFG_CONTROL_FLOW_GRAPH_H_
#define GRANARY_CFG_CONTROL_FLOW_GRAPH_H_

#include "granary/base/base.h"

namespace granary {

// Forward declarations.
class BasicBlockIterator;
class FutureBasicBlock;

class ControlFlowGraph {
 public:

  void MaterializeBasicBlock(BasicBlockIterator &iterator,
                             const FutureBasicBlock *block);

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(ControlFlowGraph);
};

}  // namespace granary

#endif  // GRANARY_CFG_CONTROL_FLOW_GRAPH_H_
