/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_RELATIVIZE_H_
#define GRANARY_DRIVER_RELATIVIZE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/pc.h"

namespace granary {

// Forward declarations.
class DecodedBasicBlock;
class ControlFlowInstruction;
class NativeInstruction;
class MemoryOperand;

namespace driver {

class Instruction;

// Relativize a direct control-flow instruction.
void RelativizeDirectCFI(ControlFlowInstruction *cfi, Instruction *instr,
                         PC target_pc, bool target_is_far_away);

// Relativize a instruction with a memory operand, where the operand loads some
// value from `mem_addr`
void RelativizeMemOp(DecodedBasicBlock *block, NativeInstruction *ninstr,
                     const MemoryOperand &op, const void *mem_addr);

// Relativize a memory operation.
//void RelativizeMemOp(NativeInstruction)


}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_RELATIVIZE_H_
