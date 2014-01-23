/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/string.h"
#include "granary/base/types.h"
#include "granary/cfg/instruction.h"
#include "granary/driver/dynamorio/instruction.h"

namespace granary {
namespace driver {

// Clear out all of the data associated with a decoded instruction.
void DecodedInstruction::Clear(void) {
  memset(this, 0, sizeof *this);
}

// Make a deep copy of a decoded instruction.
DecodedInstruction *DecodedInstruction::Copy(void) const {
  DecodedInstruction *that(new DecodedInstruction);
  memcpy(that, this, sizeof *this);

  if (instruction.srcs) {
    that->instruction.srcs = &(that->operands[instruction.srcs -
                                              &(operands[0])]);
  }
  if (instruction.dsts) {
    that->instruction.dsts = &(that->operands[instruction.dsts -
                                              &(operands[0])]);
  }
  if (instruction.note == &(raw_bytes[0])) {
    that->instruction.note = &(that->raw_bytes[0]);
  }
  if (instruction.translation == &(raw_bytes[0])) {
    that->instruction.translation = &(that->raw_bytes[0]);
  }
  if (instruction.bytes == &(raw_bytes[0])) {
    that->instruction.bytes = &(that->raw_bytes[0]);
  }

  return that;
}

ProgramCounter DecodedInstruction::BranchTarget(void) const {
  return instruction.src0.value.pc;
}

bool DecodedInstruction::IsFunctionCall(void) const {
  const unsigned op(instruction.opcode);
  return dynamorio::OP_call <= op && op <= dynamorio::OP_call_far_ind;
}

bool DecodedInstruction::IsFunctionReturn(void) const {
  const unsigned op(instruction.opcode);
  return dynamorio::OP_ret == op || dynamorio::OP_ret_far == op;
}

bool DecodedInstruction::IsInterruptCall(void) const {
  return dynamorio::OP_int == instruction.opcode;
}

bool DecodedInstruction::IsInterruptReturn(void) const {
  return dynamorio::OP_iret == instruction.opcode;
}

bool DecodedInstruction::IsSystemCall(void) const {
  return dynamorio::OP_syscall == instruction.opcode ||
         dynamorio::OP_sysenter == instruction.opcode;
}

bool DecodedInstruction::IsSystemReturn(void) const {
  return dynamorio::OP_sysret == instruction.opcode ||
         dynamorio::OP_sysexit == instruction.opcode;
}

bool DecodedInstruction::IsConditionalJump(void) const {
  const unsigned op(instruction.opcode);
  return (dynamorio::OP_jo <= op && op <= dynamorio::OP_jnle) ||
         (dynamorio::OP_jo_short <= op && op <= dynamorio::OP_jnle_short);
}

bool DecodedInstruction::IsJump(void) const {
  const unsigned op(instruction.opcode);
  return (dynamorio::OP_jmp <= op && op <= dynamorio::OP_jmp_far_ind) ||
         IsConditionalJump();
}

bool DecodedInstruction::HasIndirectTarget(void) const {
  const unsigned op(instruction.opcode);
  return IsFunctionReturn() || IsInterruptCall() || IsInterruptReturn() ||
         IsSystemCall() || IsSystemReturn() || dynamorio::OP_call_ind == op ||
         dynamorio::OP_call_far_ind == op || dynamorio::OP_jmp_ind == op ||
         dynamorio::OP_jmp_far_ind == op;
}

}  // namespace driver

bool ControlFlowInstruction::IsFunctionCall(void) const {
  return instruction->IsFunctionCall();
}

bool ControlFlowInstruction::IsFunctionReturn(void) const {
  return instruction->IsFunctionReturn();
}

bool ControlFlowInstruction::IsInterruptCall(void) const {
  return instruction->IsInterruptCall();
}

bool ControlFlowInstruction::IsInterruptReturn(void) const {
  return instruction->IsInterruptReturn();
}

bool ControlFlowInstruction::IsSystemCall(void) const {
  return instruction->IsSystemCall();
}

bool ControlFlowInstruction::IsSystemReturn(void) const {
  return instruction->IsSystemReturn();
}

bool ControlFlowInstruction::IsJump(void) const {
  return instruction->IsJump();
}

bool ControlFlowInstruction::IsConditionalJump(void) const {
  return instruction->IsConditionalJump();
}

bool ControlFlowInstruction::HasIndirectTarget(void) const {
  return instruction->HasIndirectTarget();
}

}  // namespace granary
