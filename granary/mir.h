/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_MIR_H_
#define GRANARY_MIR_H_

#include "granary/base/base.h"
#include "granary/base/types.h"

namespace granary {

class Materializer;
class BasicBlock;
class GenericMetaData;
class Instruction;
class AnnotationInstruction;

namespace mir {

// Hyper call. Ensures that machine state is saved/restored.
std::unique_ptr<Instruction> HyperCall(AppProgramCounter target_pc);

// Hyper call that doesn't ensure machine state is saved and/or restored.
std::unique_ptr<Instruction> UnsafeHyperCall(AppProgramCounter target_pc);

// Hyper jump that doesn't ensure machine state is saved and/or restored.
std::unique_ptr<Instruction> UnsafeHyperJump(AppProgramCounter target_pc);

// Call / jump to existing basic blocks.
std::unique_ptr<Instruction> Call(BasicBlock *target_block);
std::unique_ptr<Instruction> Jump(BasicBlock *target_block);

// Materialize a direct basic block and insert a direct jump to that
// basic block.
std::unique_ptr<Instruction> Jump(Materializer *materializer,
                                  AppProgramCounter target_pc);

// Materialize a direct basic block and insert a direct call to that
// basic block.
std::unique_ptr<Instruction> Call(Materializer *materializer,
                                  AppProgramCounter target_pc);

std::unique_ptr<Instruction> Jump(LocalControlFlowGraph *cfg,
                                  const AnnotationInstruction *target_instr);


}  // namespace mir
}  // namespace granary

#endif  // GRANARY_MIR_H_
