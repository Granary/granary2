/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/x86-64/builder.h"
#include "granary/arch/x86-64/instruction.h"
#include "granary/arch/x86-64/mangle/early.h"

#include "granary/base/base.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

// Append a non-native, created instruction to the block.
#define APP(...) \
  do { \
    __VA_ARGS__; \
    ni.AnalyzeStackUsage(); \
    block->UnsafeAppendInstruction(new NativeInstruction(&ni)); \
  } while (0)

// Append a "created" native instruction to the block.
#define APP_NATIVE(...) \
  do { \
    __VA_ARGS__; \
    ni.decoded_pc = instr->decoded_pc; \
    ni.AnalyzeStackUsage(); \
    block->UnsafeAppendInstruction(new NativeInstruction(&ni)); \
  } while (0)

// Append a "created" native instruction to the block, but mangle it first.
//
// Note: The recursive call to the mangler wil2l do the stack usage analysis.
#define APP_NATIVE_MANGLED(...) \
  do { \
    __VA_ARGS__; \
    ni.decoded_pc = instr->decoded_pc; \
    MangleDecodedInstruction(block, &ni); \
    block->UnsafeAppendInstruction(new NativeInstruction(&ni)); \
  } while (0)

namespace {

// Mangle an indirect call or jump through memory into a mov of the memory
// location into the `RAX` register (transparency corner case), then an
// indirect call through the register.
void MangleIndirectCFI(DecodedBasicBlock *block, Instruction *instr) {
  if (XED_ENCODER_OPERAND_TYPE_MEM == instr->ops[0].type) {
    Instruction ni;
    APP_NATIVE_MANGLED(MOV_GPRv_MEMv(&ni, XED_REG_RAX, instr->ops[0]));
    instr->ops[0].type = XED_ENCODER_OPERAND_TYPE_REG;
    instr->ops[0].reg.DecodeFromNative(static_cast<int>(XED_REG_RAX));
  }
}

// Mangle an explicit memory operand. This will expand memory operands into
// `LEA` instructions.
void MangleExplicitMemOp(DecodedBasicBlock *block, Instruction *instr) {
  Instruction ni;
  for (auto &op : instr->ops) {
    if (!op.is_explicit || XED_ENCODER_OPERAND_TYPE_INVALID == op.type) {
      break;
    } else if (XED_ENCODER_OPERAND_TYPE_MEM != op.type || !op.is_compound) {
      continue;
    }

    // All built-in memory operands, other than `XLAT`, a simple dereferences
    // of a single base register. We will convert most into non-compound
    // operands to make them easier to manipulate from the instrumentation
    // side of things.
    if (op.is_sticky) {
      if (0 == op.mem.disp && XED_REG_INVALID == op.mem.reg_index &&
          XED_REG_RSP != op.mem.reg_base) {
        op.is_compound = false;
        op.reg.DecodeFromNative(static_cast<int>(op.mem.reg_base));
      }
    } else {
      auto mem_reg = block->AllocateVirtualRegister();
      APP(LEA_GPRv_AGEN(&ni, mem_reg, op));
      op.is_compound = false;
      op.reg = mem_reg;
    }
  }
}

// Make a simple base/displacement memory operand.
static Operand BaseDispMemOp(int32_t disp, xed_reg_enum_t base_reg) {
  Operand op;
  op.type = XED_ENCODER_OPERAND_TYPE_MEM;
  op.is_compound = true;
  op.mem.disp = disp;
  op.mem.reg_base = base_reg;
  return op;
}

// Mark an instruction as potentially reading and writing to the stack.
static void AnalyzedStackUsage(Instruction *instr, bool does_read,
                               bool does_write) {
  instr->analyzed_stack_usage = true;
  instr->reads_from_stack_pointer = does_read;
  instr->writes_to_stack_pointer = does_write;
}

// Mangle a `PUSH_MEMv` instruction, where the memory operand being pushed falls
// into the user space redzone. This is a very unusual case.
//
// An example of where this type of instruction is used is in the Linux kernel
// `repeat_nmi` function. It can happen with recursive NMIs (e.g. a breakpoint
// in an NMI handler). What the kernel seems to do is have 3 copies of the NMI
// ISF: the current one (base of NMI stack, pushed on by hardware), a saved
// version of the first NMI, and a saved version of the repeated NMI. The kernel
// then tests a special location on the stack that tells it whether or not it's
// executing in a nested NMI. The kernel overwrites one of its copies with
// another copy (located just below [on the stack] the copy being overwritten).
//
// See `arch/x86/kernel/entry_64.S` in the Linux kernel source code.
//
// TODO(pag): This mangling wouldn't actually work for `repeat_nmi` because its
//            `PUSH`es could overwrite some important data.
static void ManglePushFromRedZone(DecodedBasicBlock *block, Instruction *instr,
                                  Operand op) {
  auto decoded_pc = instr->decoded_pc;
  if (-8 == op.mem.disp) {
    LEA_GPRv_AGEN(instr, XED_REG_RSP, BaseDispMemOp(-8, XED_REG_RSP));
  } else {
    Instruction ni;
    op.mem.disp += 8;  // Width of a 64-bit address.
    APP(PUSH_GPRv_50(&ni, XED_REG_RAX));
    APP_NATIVE_MANGLED(MOV_GPRv_MEMv(&ni, XED_REG_RAX, op));
    XCHG_MEMv_GPRv(instr, BaseDispMemOp(0, XED_REG_RSP), XED_REG_RAX);
  }
  instr->decoded_pc = decoded_pc;
}

// Mangle a `PUSH_MEMv`, where an value from somewhere on the stack is
// being duplicated.
//
// An example of where this type of instruction is used is in the Linux kernel
// `first_nmi` function. It duplicates one of the NMI ISFs that is higher up
// on the stack.
//
// See `arch/x86/kernel/entry_64.S` in the Linux kernel source code.
static void MangleDuplicateFromStack(DecodedBasicBlock *block,
                                     Instruction *instr, Operand op) {
  Instruction ni;
  auto decoded_pc = instr->decoded_pc;
  APP(PUSH_GPRv_50(&ni, XED_REG_RAX));
  APP_NATIVE_MANGLED(MOV_GPRv_MEMv(&ni, XED_REG_RAX,
                           BaseDispMemOp(op.mem.disp + 8, XED_REG_RSP)));
  XCHG_MEMv_GPRv(instr, BaseDispMemOp(0, XED_REG_RSP), XED_REG_RAX);
  instr->decoded_pc = decoded_pc;
}

// Generic mangling of a `PUSH_MEMv` that doesn't try to manage any of the
// corner cases.
//
// An example of where this type of instruction is used is in the Linux kernel
// `restore_registers` function. It is used to push the flags onto the stack
// before doing a `POPFQ`.
static void ManglePushMemOpGeneric(DecodedBasicBlock *block, Instruction *instr,
                                   Operand op) {
  Instruction ni;
  APP(LEA_GPRv_AGEN(&ni, XED_REG_RSP, BaseDispMemOp(-8, XED_REG_RSP)));
  APP(PUSH_GPRv_50(&ni, XED_REG_RAX));
  APP_NATIVE_MANGLED(MOV_GPRv_MEMv(&ni, XED_REG_RAX, op));
  APP(MOV_MEMv_GPRv(&ni, BaseDispMemOp(8, XED_REG_RSP), XED_REG_RAX));
  POP_GPRv_51(instr, XED_REG_RAX);
  AnalyzedStackUsage(instr, true, true);
}

// Mangle a `PUSH_MEMv` instruction.
static void ManglePushMemOp(DecodedBasicBlock *block, Instruction *instr) {
  auto op = instr->ops[0];
  if (XED_ENCODER_OPERAND_TYPE_MEM != op.type || !op.is_compound) {
    return;
  }
  // We're going to try to handle things like `PUSH (%rsp)` and `PUSH -8(%rsp)`,
  // but in practice this is undecidable because someone could first store
  // the stack pointer into a register, e.g. `%rax`, and then do `PUSH (%rax)`.
  //
  // Note: RSP cannot be the index register.
  if (XED_REG_RSP == op.mem.reg_base) {
    // Corner case: pushing memory from the redzone onto the stack.
    if (-8 >= op.mem.disp && XED_REG_INVALID == op.mem.reg_index) {
      ManglePushFromRedZone(block, instr, op);

    // Duplicating a value on the stack.
    } else if (0 <= op.mem.disp && XED_REG_INVALID == op.mem.reg_index) {
      MangleDuplicateFromStack(block, instr, op);

    // Undecidable, e.g. `PUSH -0x8(%rsp, %rbp, 1)`. We'll make the simplifying
    // assumption that it's reading from somewhere already allocated on the
    // stack, and not from somewhere in the user space redzone.
    } else {
      op.mem.disp += 8;
      ManglePushMemOpGeneric(block, instr, op);
    }
  } else {
    ManglePushMemOpGeneric(block, instr, op);
  }
}

// Generic mangling of a `POP_MEMv` that doesn't try to manage any of the
// corner cases.
//
// This is used only in a few places in the Linux kernel, usually where the
// flags are pushed onto the stack (`PUSHFQ`), and then popped off into a
// memory location for storing.
static void ManglePopMemOpGeneric(DecodedBasicBlock *block, Instruction *instr,
                                  Operand op) {
  Instruction ni;
  APP(PUSH_GPRv_50(&ni, XED_REG_RAX));
  APP(MOV_GPRv_MEMv(&ni, XED_REG_RAX, BaseDispMemOp(8, XED_REG_RSP)));
  APP_NATIVE_MANGLED(MOV_MEMv_GPRv(&ni, op, XED_REG_RAX));
  POP_GPRv_51(instr, XED_REG_RAX);
  AnalyzedStackUsage(instr, true, true);
}

// Mangle a `POP_MEMv` instruction.
static void ManglePopMemOp(DecodedBasicBlock *block, Instruction *instr) {
  auto op = instr->ops[0];
  if (XED_ENCODER_OPERAND_TYPE_MEM != op.type || !op.is_compound) {
    return;
  }
  // We're going to try to handle things like `POP (%rsp)` and `POP -8(%rsp)`.
  // This can be used for stack-to-stack memory moves.
  if (XED_REG_RSP == op.mem.reg_base) {
    GRANARY_ASSERT(false);  // TODO(pag): Not implemented.
  } else {
    ManglePopMemOpGeneric(block, instr, op);
  }
}

// Mangle an `XLAT` instruction to use virtual registers. This is to avoid the
// issue where `XLAT` is really the only instruction where two differently sized
// registers are used as a base and index register.
static void MangleXLAT(DecodedBasicBlock *block, Instruction *instr) {
  Instruction ni;
  auto addr = block->AllocateVirtualRegister();
  auto decoded_pc = instr->decoded_pc;
  APP(MOVZX_GPRv_GPR8(&ni, addr, XED_REG_AL));
  APP(LEA_GPRv_GPRv_GPRv(&ni, addr, addr, XED_REG_RBX));
  MOV_GPR8_MEMb(instr, XED_REG_AL, addr);
  instr->decoded_pc = decoded_pc;
  instr->ops[1].width = 8;
}

}  // namespace

// Perform "early" mangling of some instructions. This is primary to make the
// task of virtual register allocation tractable.
void MangleDecodedInstruction(DecodedBasicBlock *block, Instruction *instr) {
  // Do the stack usage "early" so that it is reflected in instructions
  // whose memory operands and split into intermediate `LEA` instructions.
  instr->AnalyzeStackUsage();

  switch (instr->iclass) {
    case XED_ICLASS_CALL_NEAR:
    case XED_ICLASS_JMP:
      return MangleIndirectCFI(block, instr);
    case XED_ICLASS_LEA:
      return;
    case XED_ICLASS_PUSH:
      return ManglePushMemOp(block, instr);
    case XED_ICLASS_POP:
      return ManglePopMemOp(block, instr);
    case XED_ICLASS_XLAT:
      return MangleXLAT(block, instr);
    default:
      return MangleExplicitMemOp(block, instr);
  }
}

}  // namespace arch
}  // namespace granary
