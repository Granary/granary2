/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_MIR_H_
#define GRANARY_MIR_H_

#include "granary/base/base.h"
#include "granary/base/types.h"

namespace granary {

class ControlFlowGraph;
class BasicBlock;
class Instruction;
class AnnotationInstruction;

namespace mir {

// Function call instructions.
std::unique_ptr<Instruction> Call(ControlFlowGraph *, ProgramCounter);
std::unique_ptr<Instruction> Call(ControlFlowGraph *, BasicBlock *);
std::unique_ptr<Instruction> Call(ControlFlowGraph *, VirtualRegister);

std::unique_ptr<Instruction> Jump(ControlFlowGraph *, ProgramCounter);
std::unique_ptr<Instruction> Jump(ControlFlowGraph *, BasicBlock *);
std::unique_ptr<Instruction> Jump(ControlFlowGraph *, VirtualRegister);
std::unique_ptr<Instruction> Jump(ControlFlowGraph *,
                                  const AnnotationInstruction *);


}  // namespace mir
}  // namespace granary

#endif  // GRANARY_MIR_H_
