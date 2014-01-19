/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/arch/base.h"
#include "granary/base/new.h"
#include "granary/cfg/control_flow_graph.h"

namespace granary {
namespace detail {

// Defines a list of a basic blocks within a control-flow graph.
class BasicBlockList {
 public:
  BasicBlockList *next;
  BasicBlockList *prev;
  BasicBlock *block;

  // Basic block lists are allocated from a global memory pool using the
  // `new` and `delete` operators.
  GRANARY_DEFINE_NEW_ALLOCATOR(BasicBlockList, {
    SHARED = true,
    ALIGNMENT = GRANARY_ARCH_CACHE_LINE_SIZE
  });

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(BasicBlockList);
};

}  // namespace detail

}  // namespace granary
