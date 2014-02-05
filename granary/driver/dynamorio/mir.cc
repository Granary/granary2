/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/driver.h"
#include "granary/factory.h"
#include "granary/mir.h"
#include "granary/util.h"

#include "generated/dynamorio/builder.h"

namespace granary {
namespace mir {

// Hyper call. Ensures that machine state is saved/restored.
std::unique_ptr<Instruction> HyperCall(AppProgramCounter target_pc) {
  GRANARY_UNUSED(target_pc);
  return std::unique_ptr<Instruction>(nullptr);
}

// Hyper call that doesn't ensure machine state is saved and/or restored.
std::unique_ptr<Instruction> UnsafeHyperCall(AppProgramCounter target_pc) {
  GRANARY_UNUSED(target_pc);
  return std::unique_ptr<Instruction>(nullptr);
}

// Hyper jump that doesn't ensure machine state is saved and/or restored.
std::unique_ptr<Instruction> UnsafeHyperJump(AppProgramCounter target_pc) {
  GRANARY_UNUSED(target_pc);
  return std::unique_ptr<Instruction>(nullptr);
}

// Call to an existing basic block.
std::unique_ptr<Instruction> Call(BasicBlock *target_block) {
  driver::InstructionBuilder builder;
  return std::unique_ptr<Instruction>(new ControlFlowInstruction(builder.CALL(
      dynamorio::opnd_create_pc(target_block->StartPC())), target_block));
}

// Jump to an existing basic block.
std::unique_ptr<Instruction> Jump(BasicBlock *target_block) {
  driver::InstructionBuilder builder;
  return std::unique_ptr<Instruction>(new ControlFlowInstruction(builder.JMP(
      dynamorio::opnd_create_pc(target_block->StartPC())), target_block));
}

// Materialize a future basic block and insert a direct jump to that
// basic block.
std::unique_ptr<Instruction> Jump(BlockFactory *materializer,
                                  AppProgramCounter target_pc) {
  auto target_block = materializer->Materialize(target_pc);
  driver::InstructionBuilder builder;
  return std::unique_ptr<Instruction>(new ControlFlowInstruction(builder.JMP(
      dynamorio::opnd_create_pc(target_pc)), target_block.release()));

}

// Materialize a future basic block and insert a direct call to that
// basic block.
std::unique_ptr<Instruction> Call(BlockFactory *materializer,
                                  AppProgramCounter target_pc) {
  auto target_block = materializer->Materialize(target_pc);
  driver::InstructionBuilder builder;
  return std::unique_ptr<Instruction>(new ControlFlowInstruction(builder.CALL(
      dynamorio::opnd_create_pc(target_pc)), target_block.release()));

}

}  // namespace mir
}  // namespace granary
