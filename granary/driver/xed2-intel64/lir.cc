/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/string.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/driver/xed2-intel64/builder.h"

#include "granary/ir/lir.h"

namespace granary {
namespace lir {

// Call to an existing basic block.
std::unique_ptr<Instruction> Call(BasicBlock *target_block) {
  driver::Instruction instr;
  CALL_NEAR_RELBRd(&instr, target_block->StartAppPC());
  return std::unique_ptr<Instruction>(
      new ControlFlowInstruction(&instr, target_block));
}

// Jump to an existing basic block.
std::unique_ptr<Instruction> Jump(BasicBlock *target_block) {
  driver::Instruction instr;
  JMP_RELBRd(&instr, target_block->StartAppPC());
  return std::unique_ptr<Instruction>(
      new ControlFlowInstruction(&instr, target_block));
}

}  // namespace lir
}  // namespace granary
