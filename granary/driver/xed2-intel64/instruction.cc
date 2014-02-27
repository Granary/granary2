/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/string.h"
#include "granary/base/pc.h"

#include "granary/driver/xed2-intel64/decode.h"
#include "granary/driver/xed2-intel64/instruction.h"

#include "granary/breakpoint.h"

namespace granary {
namespace driver {

// Get the PC-relative branch target.
PC Instruction::BranchTarget(void) const {
  return ops[0].rel.pc;  // TODO(pag): CALL_FAR, JMP_FAR?
}

// Set the PC-relative branch target.
//
// Note: Don't need to modify `needs_encoding` because
void Instruction::SetBranchTarget(PC pc) {
  ops[0].rel.pc = pc;
}

bool Instruction::IsFunctionCall(void) const {
  return XED_CATEGORY_CALL == category;
}

bool Instruction::IsFunctionReturn(void) const {
  return XED_ICLASS_RET_FAR == iclass || XED_ICLASS_RET_NEAR == iclass;
}

bool Instruction::IsInterruptCall(void) const {
  return XED_CATEGORY_INTERRUPT == category;
}

bool Instruction::IsInterruptReturn(void) const {
  return XED_ICLASS_IRET == iclass || XED_ICLASS_IRETD == iclass ||
         XED_ICLASS_IRETQ == iclass;
}

bool Instruction::IsSystemCall(void) const {
  return XED_CATEGORY_SYSCALL == category;
}

bool Instruction::IsSystemReturn(void) const {
  return XED_CATEGORY_SYSRET == category;
}

bool Instruction::IsConditionalJump(void) const {
  return XED_CATEGORY_COND_BR == category;
}

bool Instruction::IsUnconditionalJump(void) const {
  // TODO(pag): XABORT is included in this op category.
  return XED_CATEGORY_UNCOND_BR == category;
}

bool Instruction::IsJump(void) const {
  return IsUnconditionalJump() || IsConditionalJump();
}

bool Instruction::HasIndirectTarget(void) const {
  if (IsFunctionCall() || IsUnconditionalJump()) {
    return !has_pc_rel_op;
  }
  return IsFunctionReturn() || IsInterruptCall() || IsInterruptReturn() ||
         IsSystemCall() || IsSystemReturn();
}

// Return the (current) length of the instruction.
int Instruction::Length(void) const {
  GRANARY_ASSERT(!has_virtual_reg_op);
  if (GRANARY_UNLIKELY(needs_encoding && !has_pc_rel_op)) {
    InstructionDecoder().Encode(const_cast<Instruction *>(this), nullptr);
  }
  return length;
}

// Return the (current) length of the instruction.
bool Instruction::IsNoOp(void) const {
  return XED_ICLASS_NOP == iclass;
}

}  // namespace driver
}  // namespace granary
