/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/factory.h"
#include "granary/cfg/instruction.h"

#include "granary/ir/lir.h"

namespace granary {
namespace lir {

// Materialize a future basic block and insert a direct jump to that
// basic block.
std::unique_ptr<Instruction> Jump(BlockFactory *factory, AppPC target_pc) {
  auto target_block = factory->Materialize(target_pc);
  return Jump(target_block.release());
}

// Materialize a future basic block and insert a direct call to that
// basic block.
std::unique_ptr<Instruction> Call(BlockFactory *factory, AppPC target_pc) {
  auto target_block = factory->Materialize(target_pc);
  return Call(target_block.release());
}

}  // namespace lir
}  // namespace granary
