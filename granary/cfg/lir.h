/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_LIR_H_
#define GRANARY_CFG_LIR_H_

#include "arch/context.h"

#include "granary/base/base.h"
#include "granary/base/pc.h"

#include "granary/cfg/factory.h"

namespace granary {

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
std::unique_ptr<Instruction> Jump(BlockFactory *factory, AppPC target_pc,
                                  BlockRequestKind request=REQUEST_LATER);

// Materialize a direct basic block and insert a direct call to that
// basic block.
std::unique_ptr<Instruction> Call(BlockFactory *factory, AppPC target_pc,
                                  BlockRequestKind request=REQUEST_LATER);

// Materialize a return from a function.
std::unique_ptr<Instruction> Return(BlockFactory *factory);

std::unique_ptr<Instruction> Jump(const LabelInstruction *target_instr);

// Call to a client function that takes in an argument to an
// `arch::MachineContext` pointer.
//
// A context call does not allow one to see intermediate virtual register
// state. Therefore, context calls do not have access to virtual registers.
// This limits there applicability to places where the instrumentation code
// wants to see the native machine context as it would be without
std::unique_ptr<Instruction> CallWithContext(
    void (*func)(arch::MachineContext *));

// TODO(pag): OutlineCall, add a function that simply calls to some client code
//            and has access to virtual registers.
// TODO(pag): InlineCall, inline the code of a function directly into the
//            code.

}  // namespace lir
}  // namespace granary

#endif  // GRANARY_CFG_LIR_H_
