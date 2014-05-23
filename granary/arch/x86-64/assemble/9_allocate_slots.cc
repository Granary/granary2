/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

// Append a non-native, created instruction to the block.
#define INSERT_AFTER(...) \
  do { \
    __VA_ARGS__; \
    frag->instrs.InsertAfter(instr, new NativeInstruction(&ni)); \
  } while (0)

#include "granary/arch/x86-64/builder.h"

#include "granary/cfg/instruction.h"

#include "granary/code/assemble/fragment.h"

namespace granary {

// Returns a new instruction that will allocate some stack space for virtual
// register slots.
NativeInstruction *AllocateStackSpace(int num_bytes) {
  arch::Instruction instr;
  LEA_GPRv_AGEN(&instr, XED_REG_RSP,
                arch::BaseDispMemOp(num_bytes, XED_REG_RSP, 64));
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

// Mangle `PUSH_GPRv_*` into a `MOV_MEMv_GPRv` that simulates the `PUSH`
// instruction. We don't need to simulate changes to the stack pointer.
static void ManglePush(NativeInstruction *instr, int adjusted_offset) {
  auto op = instr->instruction.ops[0];
  if (op.IsRegister()) {
    auto mem_width = instr->instruction.effective_operand_width;
    GRANARY_ASSERT(-1 != mem_width);
    MOV_MEMv_GPRv(
        &(instr->instruction),
        arch::BaseDispMemOp(adjusted_offset, XED_REG_RSP, mem_width),
        op.reg);

  // Things like `PUSH_IMMv`, `PUSH_FS/GS`, and `PUSH_MEMv` should have already
  // been early mangled.
  } else {
    GRANARY_ASSERT(false);
  }
}

// Mangle `POP_GPRv_*` into a `MOV_GPRv_MEMv` that simulates the `PUSH`
// instruction. We don't need to simulate changes to the stack pointer.
static void ManglePop(NativeInstruction *instr, int adjusted_offset) {
  auto op = instr->instruction.ops[0];
  if (op.IsRegister()) {
    auto mem_width = instr->instruction.effective_operand_width;
    GRANARY_ASSERT(-1 != mem_width);
    MOV_GPRv_MEMv(
        &(instr->instruction),
        op.reg,
        arch::BaseDispMemOp(adjusted_offset, XED_REG_RSP, mem_width));

  // Things like `POP_FS/GS` and `POP_MEMv` should have already been
  // early mangled.
  } else {
    GRANARY_ASSERT(false);
  }
}

// Returns true if an architectural operand looks like a spill slot.
static bool IsSpillSlot(const arch::Operand &op) {
  return op.IsMemory() && !op.is_compound && op.reg.IsVirtualSlot();
}

// Mangle a `MOV_GPRv_MEMv` or `MOV_MEMv_GPRv` instruction, where the `MEMv`
// operand is assumed to be a abstract spill slot.
static void MangleMov(NativeInstruction *instr) {
  auto &ainstr(instr->instruction);
  arch::Operand *mem_op(nullptr);
  if (IsSpillSlot(ainstr.ops[0])) {
    mem_op = &(ainstr.ops[0]);
  } else if (IsSpillSlot(ainstr.ops[1])) {
    mem_op = &(ainstr.ops[1]);
  }
  if (mem_op) {
    const auto new_mem_op = arch::BaseDispMemOp(mem_op->reg.Number() * 8,
                                                XED_REG_RSP, 64);
    mem_op->mem = new_mem_op.mem;
    mem_op->is_compound = new_mem_op.is_compound;
  }
}

}  // namespace

// Adjusts / mangles an instruction (potentially more than one) so that the
// usage of the stack pointer remains transparent, despite the fact that the
// native stack pointer has been changed to accommodate virtual register spills.
// Returns the next instruction on which we should operate.
//
// Note: This function has an architecture-specific implementation.
Instruction *AdjustStackInstruction(Fragment *frag,
                                    NativeInstruction *instr,
                                    int adjusted_offset,
                                    int *next_offset) {
  GRANARY_UNUSED(next_offset);
  GRANARY_UNUSED(frag);

  auto &ainstr(instr->instruction);
  const auto next = instr->Next();

  switch (ainstr.iclass) {
    case XED_ICLASS_PUSH:
      ManglePush(instr, adjusted_offset);
      break;
    case XED_ICLASS_POP:
      ManglePop(instr, adjusted_offset);
      break;
    case XED_ICLASS_PUSHF:
    case XED_ICLASS_PUSHFQ:
    case XED_ICLASS_POPF:
    case XED_ICLASS_POPFQ:
    case XED_ICLASS_RET_NEAR:
    case XED_ICLASS_MOV:
      MangleMov(instr);
      break;

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
  return next;
}

}  // namespace granary
