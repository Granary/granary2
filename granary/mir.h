/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_MIR_H_
#define GRANARY_MIR_H_

#include "granary/base/base.h"
#include "granary/base/types.h"

namespace granary {

class LocalControlFlowGraph;
class BasicBlock;
class GenericMetaData;
class Instruction;
class AnnotationInstruction;

namespace mir {

// Function call instructions.
std::unique_ptr<Instruction> Call(LocalControlFlowGraph *cfg,
                                  BasicBlock *target_block);

// Variants of direct jump instructions.
std::unique_ptr<Instruction> Jump(LocalControlFlowGraph *cfg,
                                  BasicBlock *target_block);

std::unique_ptr<Instruction> Jump(LocalControlFlowGraph *cfg,
                                  AppProgramCounter target_pc);

std::unique_ptr<Instruction> Jump(LocalControlFlowGraph *cfg,
                                  const AnnotationInstruction *target_instr);


}  // namespace mir
}  // namespace granary

#endif  // GRANARY_MIR_H_
