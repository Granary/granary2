/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/lir.h"
#include "granary/cfg/operand.h"

#include "arch/x86-64/builder.h"

#include "granary/breakpoint.h"

namespace granary {
namespace lir {

// Indirect jump to an existing basic block.
std::unique_ptr<ControlFlowInstruction> IndirectJump(
    BasicBlock *target_block, const granary::Operand &op) {
  arch::Instruction instr;
  if (auto mem = DynamicCast<granary::MemoryOperand *>(&op)) {
    const void *ptr(nullptr);
    VirtualRegister reg;
    if (mem->MatchPointer(ptr)) {
      JMP_MEMv(&instr, ptr);
    } else if (mem->MatchRegister(reg)) {
      JMP_MEMv(&instr, reg);
    } else {
      GRANARY_ASSERT(false);
    }
  } else if (auto reg = DynamicCast<granary::RegisterOperand *>(&op)) {
    JMP_GPRv(&instr, reg->Register());
  } else {
    GRANARY_ASSERT(false);
  }
  return std::unique_ptr<ControlFlowInstruction>(
      new ControlFlowInstruction(&instr, target_block));
}

// Call to an existing basic block.
std::unique_ptr<ControlFlowInstruction> Call(BasicBlock *target_block) {
  arch::Instruction instr;
  CALL_NEAR_RELBRd(&instr, target_block->StartAppPC());
  return std::unique_ptr<ControlFlowInstruction>(
      new ControlFlowInstruction(&instr, target_block));
}

// Jump to an existing basic block.
std::unique_ptr<ControlFlowInstruction> Jump(BasicBlock *target_block) {
  arch::Instruction instr;
  JMP_RELBRd(&instr, target_block->StartAppPC());
  return std::unique_ptr<ControlFlowInstruction>(
      new ControlFlowInstruction(&instr, target_block));
}

}  // namespace lir
}  // namespace granary
