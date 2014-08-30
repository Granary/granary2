/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/factory.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/lir.h"

namespace granary {
namespace lir {

// Materialize a future basic block and insert a direct jump to that
// basic block.
std::unique_ptr<Instruction> Jump(BlockFactory *factory, AppPC target_pc,
                                  BlockRequestKind request) {
  auto block = factory->Materialize(target_pc);
  factory->RequestBlock(block, request);
  return Jump(block);
}

// Materialize a future basic block and insert a direct call to that
// basic block.
std::unique_ptr<Instruction> Call(BlockFactory *factory, AppPC target_pc,
                                  BlockRequestKind request) {
  auto block = factory->Materialize(target_pc);
  factory->RequestBlock(block, request);
  return Call(block);
}

}  // namespace lir
}  // namespace granary
