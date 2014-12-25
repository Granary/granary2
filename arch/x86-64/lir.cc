/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/cfg/block.h"
#include "granary/cfg/instruction.h"
#include "granary/cfg/lir.h"
#include "granary/cfg/operand.h"

#include "arch/x86-64/builder.h"

#include "granary/breakpoint.h"

namespace granary {
namespace lir {

// Indirect jump to an existing basic block.
std::unique_ptr<Instruction> IndirectJump(Block *target_block,
                                          const granary::Operand &op) {

  arch::Instruction ni;
  if (auto mem = DynamicCast<granary::MemoryOperand *>(&op)) {
    const void *ptr(nullptr);
    VirtualRegister reg;
    if (mem->MatchPointer(ptr)) {
      JMP_MEMv(&ni, ptr);
    } else if (mem->MatchRegister(reg)) {
      JMP_MEMv(&ni, reg);
    } else {
      GRANARY_ASSERT(false);
    }
  } else if (auto reg = DynamicCast<granary::RegisterOperand *>(&op)) {
    JMP_GPRv(&ni, reg->Register());
  } else {
    GRANARY_ASSERT(false);
  }
  return std::unique_ptr<Instruction>(
      new ControlFlowInstruction(&ni, target_block));
}

// Call to an existing basic block.
std::unique_ptr<Instruction> FunctionCall(Block *target_block) {
  arch::Instruction ni;
  CALL_NEAR_RELBRd(&ni, target_block->StartAppPC());
  return std::unique_ptr<Instruction>(
      new ControlFlowInstruction(&ni, target_block));
}

// Jump to an existing basic block.
std::unique_ptr<Instruction> Jump(Block *target_block) {
  arch::Instruction ni;
  JMP_RELBRd(&ni, target_block->StartAppPC());
  return std::unique_ptr<Instruction>(
      new ControlFlowInstruction(&ni, target_block));
}

// Materialize a return from a function.
std::unique_ptr<Instruction> Return(BlockFactory *factory) {
  arch::Instruction ni;
  RET_NEAR(&ni);
  ni.effective_operand_width = arch::ADDRESS_WIDTH_BITS;
  return std::unique_ptr<Instruction>(factory->MakeInstruction(&ni, &ni));
}

// Conversion functions.
void ConvertFunctionCallToJump(ControlFlowInstruction *cfi) {
  auto &ni(cfi->instruction);
  if (ni.HasIndirectTarget()) {
    if (ni.ops[0].IsRegister()) {
      JMP_GPRv(&ni, ni.ops[0]);
    } else {
      JMP_MEMv(&ni, ni.ops[0]);
    }
  } else if (ni.ops[0].is_annotation_instr) {
    JMP_RELBRd(&ni, ni.ops[0].annotation_instr);
  } else {
    JMP_RELBRd(&ni, ni.ops[0].branch_target.as_pc);
  }
  ni.is_tail_call = true;
}

void ConvertJumpToFunctionCall(ControlFlowInstruction *cfi) {
  auto &ni(cfi->instruction);
  if (ni.HasIndirectTarget()) {
    if (ni.ops[0].IsRegister()) {
      CALL_NEAR_GPRv(&ni, ni.ops[0]);
    } else {
      CALL_NEAR_MEMv(&ni, ni.ops[0]);
    }
  } else if (ni.ops[0].is_annotation_instr) {
    CALL_NEAR_RELBRd(&ni, ni.ops[0].annotation_instr);
  } else {
    CALL_NEAR_RELBRd(&ni, ni.ops[0].branch_target.as_pc);
  }
}

}  // namespace lir
}  // namespace granary
