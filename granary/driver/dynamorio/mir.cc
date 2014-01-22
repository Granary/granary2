/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/mir.h"
#include "granary/cfg/instruction.h"

namespace granary {
namespace mir {

// Function call instructions.
std::unique_ptr<Instruction> Call(ControlFlowGraph *, ProgramCounter) {
  return std::unique_ptr<Instruction>(nullptr);
}

std::unique_ptr<Instruction> Call(ControlFlowGraph *, BasicBlock *) {
  return std::unique_ptr<Instruction>(nullptr);
}

std::unique_ptr<Instruction> Call(ControlFlowGraph *, VirtualRegister) {
  return std::unique_ptr<Instruction>(nullptr);
}

std::unique_ptr<Instruction> Jump(ControlFlowGraph *, ProgramCounter) {
  return std::unique_ptr<Instruction>(nullptr);
}

std::unique_ptr<Instruction> Jump(ControlFlowGraph *, BasicBlock *) {
  return std::unique_ptr<Instruction>(nullptr);
}

std::unique_ptr<Instruction> Jump(ControlFlowGraph *, VirtualRegister) {
  return std::unique_ptr<Instruction>(nullptr);
}

std::unique_ptr<Instruction> Jump(ControlFlowGraph *,
                                  const AnnotationInstruction *) {
  return std::unique_ptr<Instruction>(nullptr);
}

}  // namespace mir
}  // namespace granary
