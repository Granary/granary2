/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/driver.h"
#include "granary/mir.h"

#include "generated/dynamorio/builder.h"

namespace granary {
namespace mir {

// Function call instructions.
std::unique_ptr<Instruction> Call(ControlFlowGraph *, BasicBlock *) {
  return std::unique_ptr<Instruction>(nullptr);
}

std::unique_ptr<Instruction> Jump(ControlFlowGraph *,
                                  BasicBlock *target_block) {
  driver::InstructionBuilder builder;
  return std::unique_ptr<Instruction>(new ControlFlowInstruction(builder.JMP(
      dynamorio::opnd_create_pc(target_block->app_start_pc)),target_block));
}

std::unique_ptr<Instruction> Jump(ControlFlowGraph *,
                                  const AnnotationInstruction *) {
  return std::unique_ptr<Instruction>(nullptr);
}

std::unique_ptr<Instruction> Jump(ControlFlowGraph *cfg,
                                  AppProgramCounter target_pc) {
  return Jump(cfg, cfg->Materialize(target_pc));
}

}  // namespace mir
}  // namespace granary
