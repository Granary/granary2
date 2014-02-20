/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_IR_LIR_H_
#define GRANARY_IR_LIR_H_

#include "granary/base/base.h"
#include "granary/base/types.h"

namespace granary {

class BlockFactory;
class BasicBlock;
class Instruction;
class AnnotationInstruction;

namespace lir {

// Call / jump to existing basic blocks.
std::unique_ptr<Instruction> Call(BasicBlock *target_block);
std::unique_ptr<Instruction> Jump(BasicBlock *target_block);

// Materialize a direct basic block and insert a direct jump to that
// basic block.
std::unique_ptr<Instruction> Jump(BlockFactory *factory, AppPC target_pc);

// Materialize a direct basic block and insert a direct call to that
// basic block.
std::unique_ptr<Instruction> Call(BlockFactory *factory, AppPC target_pc);

std::unique_ptr<Instruction> Jump(LocalControlFlowGraph *cfg,
                                  const AnnotationInstruction *target_instr);

}  // namespace lir
}  // namespace granary

#endif  // GRANARY_IR_LIR_H_
