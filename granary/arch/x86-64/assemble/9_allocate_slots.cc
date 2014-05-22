/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/builder.h"

#include "granary/cfg/instruction.h"

namespace granary {

// Returns a new instruction that will allocate some stack space for virtual
// register slots.
NativeInstruction *AllocateStackSpace(int num_bytes) {
  arch::Instruction instr;
  LEA_GPRv_AGEN(&instr, XED_REG_RSP,
                arch::BaseDispMemOp(num_bytes, XED_REG_RSP));
  instr.AnalyzeStackUsage();
  return new NativeInstruction(&instr);
}

// Returns a new instruction that will allocate some stack space allocated
// for virtual registers. The amount of space freed does not necessarily
// correspond to the amount allocated, but instead corresponds to how the
// native stack pointer has changed since virtual registers were allocated.
NativeInstruction *FreeStackSpace(int num_bytes) {
  return AllocateStackSpace(num_bytes);
}

namespace {

static void ManglePush(NativeInstruction *instr, int adjusted_offset) {
  auto op = instr->instruction.ops[0];
  if (op.IsRegister()) {
    GRANARY_UNUSED(adjusted_offset);
  } else {
    GRANARY_ASSERT(false);
  }
}

}  // namespace

// Adjusts / mangles an instruction (potentially more than one) so that the
// usage of the stack pointer remains transparent, despite the fact that the
// native stack pointer has been changed to accommodate virtual register spills.
// Returns the next instruction on which we should operate.
//
// Note: This function has an architecture-specific implementation.
Instruction *AdjustStackInstruction(NativeInstruction *instr,
                                    int adjusted_frame_size,
                                    int offset, int *next_offset) {
  GRANARY_UNUSED(next_offset);

  auto adjusted_offset = offset - adjusted_frame_size;

  auto &ainstr(instr->instruction);
  switch (ainstr.iclass) {
    case XED_ICLASS_PUSH:
      ManglePush(instr, adjusted_offset);
      break;
    case XED_ICLASS_POP:
    case XED_ICLASS_PUSHF:
    case XED_ICLASS_PUSHFQ:
    case XED_ICLASS_POPF:
    case XED_ICLASS_POPFQ:
    case XED_ICLASS_RET_NEAR:
    case XED_ICLASS_MOV:
    case XED_ICLASS_LEA:
      // TODO!
      break;

    // Shouldn't be seen!
    case XED_ICLASS_CALL_NEAR:
    case XED_ICLASS_CALL_FAR:
    case XED_ICLASS_RET_FAR:
    case XED_ICLASS_IRET:
    case XED_ICLASS_INT3:
    case XED_ICLASS_INT:
    case XED_ICLASS_BOUND:
    case XED_ICLASS_PUSHFD:
    case XED_ICLASS_POPFD:
      GRANARY_ASSERT(false);
      break;

    default:
      // TODO!
      break;
  }
  return instr->Next();
}

}
