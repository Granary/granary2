/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/string.h"
#include "granary/cfg/instruction.h"
#include "granary/driver/dynamorio/instruction.h"

namespace granary {
namespace driver {

void DecodedInstruction::Clear(void) {
  memset(this, 0, sizeof *this);
}


void DecodedInstruction::Copy(const DecodedInstruction *that) {
  if (this == that) {
    return;
  }

  memcpy(this, that, sizeof *this);

  if (instruction.srcs) {
    instruction.srcs = &(operands[that->instruction.srcs -
                                  &(that->operands[0])]);
  }
  if (instruction.dsts) {
    instruction.dsts = &(operands[that->instruction.dsts -
                                  &(that->operands[0])]);
  }
  if (instruction.note == &(that->raw_bytes[0])) {
    instruction.note = &(raw_bytes[0]);
  }
  if (instruction.translation == &(that->raw_bytes[0])) {
    instruction.translation = &(raw_bytes[0]);
  }
  if (instruction.bytes == &(that->raw_bytes[0])) {
    instruction.bytes = &(raw_bytes[0]);
  }
}

}  // namespace driver

bool ControlFlowInstruction::IsFunctionCall(void) const {
  const unsigned op(instruction->instruction.opcode);
  return dynamorio::OP_call <= op && op <= dynamorio::OP_call_far_ind;
}

bool ControlFlowInstruction::IsFunctionReturn(void) const {
  return dynamorio::OP_ret == instruction->instruction.opcode ||
         dynamorio::OP_ret_far == instruction->instruction.opcode;
}

bool ControlFlowInstruction::IsInterruptReturn(void) const {
  return dynamorio::OP_iret == instruction->instruction.opcode;
}

bool ControlFlowInstruction::IsJump(void) const {
  const unsigned op(instruction->instruction.opcode);
  return (dynamorio::OP_jmp <= op && op <= dynamorio::OP_jmp_far_ind) ||
         IsConditionalJump();
}

bool ControlFlowInstruction::IsConditionalJump(void) const {
  const unsigned op(instruction->instruction.opcode);
  return (dynamorio::OP_jb <= op && op <= dynamorio::OP_jnle) ||
         (dynamorio::OP_jb_short <= op && op <= dynamorio::OP_jnle_short);
}

bool ControlFlowInstruction::HasIndirectTarget(void) const {
  const unsigned op(instruction->instruction.opcode);
  return IsFunctionReturn() || IsInterruptReturn() ||
         dynamorio::OP_call_ind == op || dynamorio::OP_call_far_ind == op ||
         dynamorio::OP_jmp_ind == op || dynamorio::OP_jmp_far_ind;
}

}  // namespace granary
