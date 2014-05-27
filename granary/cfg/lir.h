/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_LIR_H_
#define GRANARY_CFG_LIR_H_

#include "granary/base/base.h"
#include "granary/base/pc.h"

namespace granary {

class BlockFactory;
class BasicBlock;
class Instruction;
class AnnotationInstruction;
class Operand;

namespace lir {

// Indirect jump to an existing basic block.
std::unique_ptr<Instruction> IndirectJump(BasicBlock *target_block,
                                          const Operand &op);

// Call / jump to existing basic blocks.
std::unique_ptr<Instruction> Call(BasicBlock *target_block);
std::unique_ptr<Instruction> Jump(BasicBlock *target_block);

// Materialize a direct basic block and insert a direct jump to that
// basic block.
std::unique_ptr<Instruction> Jump(BlockFactory *factory, AppPC target_pc);

// Materialize a direct basic block and insert a direct call to that
// basic block.
std::unique_ptr<Instruction> Call(BlockFactory *factory, AppPC target_pc);

std::unique_ptr<Instruction> Jump(const LabelInstruction *target_instr);

}  // namespace lir
}  // namespace granary

#endif  // GRANARY_CFG_LIR_H_
