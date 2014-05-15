/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/x86-64/builder.h"
#include "granary/arch/x86-64/early_mangle.h"
#include "granary/arch/x86-64/instruction.h"

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
    MangleDecodedInstruction(block, &ni, true); \
    block->UnsafeAppendInstruction(new NativeInstruction(&ni)); \
  } while (0)

namespace {

// Mangle an indirect CALL or JMP through memory into a MOV of the memory
// location into a virtual register, then an indirect call through the
// virtual register.
void MangleIndirectCFI(DecodedBasicBlock *block, Instruction *instr) {
  if (XED_ENCODER_OPERAND_TYPE_MEM == instr->ops[0].type) {
    Instruction ni;
    auto target_loc = block->AllocateVirtualRegister();
    APP_NATIVE_MANGLED(MOV_GPRv_MEMv(&ni, target_loc, instr->ops[0]));
    instr->ops[0].type = XED_ENCODER_OPERAND_TYPE_REG;
    instr->ops[0].reg = target_loc;
    instr->ops[0].is_sticky = false;
  }
}

// Mangle an explicit memory operand. This will expand memory operands into
// `LEA` instructions.
void MangleExplicitMemOp(DecodedBasicBlock *block, Operand &op) {
  Instruction ni;

  // Special consideration is given to non-compound stack operands, e.g.
  // `MOV ..., [RSP]`. Because we might be changing the stack pointer, we
  // bring those operands out into their own instructions early on so that we
  // can potentially alter what the offset to them is later on (in the event
  // that virtual regs are spilled to the stack).
  if (!op.is_compound && !op.reg.IsStackPointer()) {

    // Minor optimization attempt to move de-references of RAX outside of flag
    // zones. In the worst case, we'll likely just remove the resulting `LEA`
    // instruction with copy propagation.
    if (XED_REG_RAX != op.reg.EncodeToNative()) {
      return;
    }
  }

  // All built-in memory operands, other than `XLAT`, a simple dereferences
  // of a single base register. We will convert most into non-compound
  // operands to make them easier to manipulate from the instrumentation
  // side of things.
  if (op.is_sticky) {
    if (0 == op.mem.disp && XED_REG_INVALID == op.mem.reg_index) {
      GRANARY_ASSERT(XED_REG_RSP != op.mem.reg_base);
      op.is_compound = false;
      op.reg.DecodeFromNative(static_cast<int>(op.mem.reg_base));
    }
  } else {
    auto mem_reg = block->AllocateVirtualRegister();
    if (op.is_compound) {
      if (XED_REG_RSP == op.mem.reg_base) {
        mem_reg.ConvertToVirtualStackPointer();
      }
    } else {
      if (op.reg.IsStackPointer()) mem_reg.ConvertToVirtualStackPointer();
    }
    APP(LEA_GPRv_AGEN(&ni, mem_reg, op));
    op.is_compound = false;
    op.reg = mem_reg;
  }
}

// Make a simple base/displacement memory operand.
static Operand BaseDispMemOp(int32_t disp, xed_reg_enum_t base_reg) {
  Operand op;
  op.type = XED_ENCODER_OPERAND_TYPE_MEM;
  if (disp) {
    op.is_compound = true;
    op.mem.disp = disp;
    op.mem.reg_base = base_reg;
  } else {
    op.is_compound = false;
    op.reg.DecodeFromNative(base_reg);
  }
  return op;
}

// Add in an extra instruction for a read from the stack pointer. The purpose
// of this is that if an instruction reads from the stack pointer, then we'll
// potentially need to emulate what the intended stack pointer read is later
// on when virtual register spilling might have changed the actual stack
// pointer.
static void MangleExplicitStackPointerRegOp(DecodedBasicBlock *block,
                                            Operand &op) {
  if (!op.IsWrite()) {
    Instruction ni;
    auto sp = block->AllocateVirtualRegister();
    sp.ConvertToVirtualStackPointer();
    APP(LEA_GPRv_AGEN(&ni, sp, BaseDispMemOp(0, XED_REG_RSP)));
    sp.Widen(op.reg.ByteWidth());
    op.reg = sp;  // Replace the operand.
  }
}

// Mangle an explicit memory operand. This will expand memory operands into
// `LEA` instructions.
void MangleExplicitOps(DecodedBasicBlock *block, Instruction *instr) {
  Instruction ni;
  for (auto &op : instr->ops) {
    if (op.is_explicit) {
      if (XED_ENCODER_OPERAND_TYPE_MEM == op.type) {
        MangleExplicitMemOp(block, op);

      } else if (op.IsRegister() && op.reg.IsStackPointer()) {
        MangleExplicitStackPointerRegOp(block, op);
      }
    }
  }
}

// Mark an instruction as potentially reading and writing to the stack.
static void AnalyzedStackUsage(Instruction *instr, bool does_read,
                               bool does_write) {
  instr->analyzed_stack_usage = true;
  instr->reads_from_stack_pointer = does_read;
  instr->writes_to_stack_pointer = does_write;
}

// Mangle a `PUSH_MEMv` instruction.
static void ManglePushMemOp(DecodedBasicBlock *block, Instruction *instr) {
  auto op = instr->ops[0];
  if (XED_ENCODER_OPERAND_TYPE_MEM == op.type) {
    Instruction ni;
    auto vr = block->AllocateVirtualRegister();
    APP_NATIVE_MANGLED(MOV_GPRv_MEMv(&ni, vr, op));
    APP(MOV_MEMv_GPRv(&ni, BaseDispMemOp(0, XED_REG_RSP), vr));
    LEA_GPRv_AGEN(instr, XED_REG_RSP, BaseDispMemOp(-8, XED_REG_RSP));
    AnalyzedStackUsage(instr, true, true);
  }
}

// Mangle a `POP_MEMv` instruction.
static void ManglePopMemOp(DecodedBasicBlock *block, Instruction *instr) {
  auto op = instr->ops[0];
  Instruction ni;
  auto vr = block->AllocateVirtualRegister();
  APP(MOV_GPRv_MEMv(&ni, vr, BaseDispMemOp(0, XED_REG_RSP)));
  APP_NATIVE_MANGLED(MOV_MEMv_GPRv(&ni, op, vr));
  LEA_GPRv_AGEN(instr, XED_REG_RSP, BaseDispMemOp(8, XED_REG_RSP));
  AnalyzedStackUsage(instr, true, true);
}

// Mangle a `POP_GPRv` instruction, where the popped GPR is the stack pointer.
static void ManglePopStackPointer(DecodedBasicBlock *block,
                                  Instruction *instr) {
  auto decoded_pc = instr->decoded_pc;
  MOV_GPRv_MEMv(instr, instr->ops[0].reg, BaseDispMemOp(0, XED_REG_RSP));
  instr->decoded_pc = decoded_pc;
  AnalyzedStackUsage(instr, true, true);
  MangleDecodedInstruction(block, instr, true);
}

// Mangle a `POP_*` instruction.
static void ManglePop(DecodedBasicBlock *block, Instruction *instr) {
  auto op = instr->ops[0];
  if (XED_ENCODER_OPERAND_TYPE_MEM == op.type) {
    ManglePopMemOp(block, instr);
  } else if (op.IsRegister() && op.reg.IsStackPointer()) {
    ManglePopStackPointer(block, instr);
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

// Mangle an `ENTER` instruction.
static void MangleEnter(DecodedBasicBlock *block, Instruction *instr) {
  Instruction ni;
  auto frame_size = instr->ops[0].imm.as_uint & 0xFFFFUL;
  auto num_args = instr->ops[1].imm.as_uint & 0x1FUL;
  auto temp_rbp = block->AllocateVirtualRegister();
  auto decoded_pc = instr->decoded_pc;
  temp_rbp.ConvertToVirtualStackPointer();
  APP_NATIVE(LEA_GPRv_AGEN(&ni, temp_rbp, BaseDispMemOp(0, XED_REG_RSP)));
  APP_NATIVE(PUSH_GPRv_50(&ni, XED_REG_RBP));

  // In the case of something like watchpoints, where `RBP` is being tracked,
  // and where the application is doing something funky with `RBP` (e.g. it's
  // somehow watched), then we want to see these memory writes.
  for (auto i = 0UL; i < num_args; ++i) {
    auto offset = -static_cast<int32_t>(i * arch::ADDRESS_WIDTH_BYTES);
    APP_NATIVE_MANGLED(PUSH_MEMv(&ni, BaseDispMemOp(offset, XED_REG_RBP)));
  }

  if (frame_size) {
    APP(LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                      BaseDispMemOp(-static_cast<int32_t>(frame_size),
                                    XED_REG_RSP)));

    // Enter finishes with a memory write that is "unused". This is to detect
    // stack segment issues and page faults. We don't even bother with this
    // because emulating the exception behavior of `ENTER` is pointless because
    // it could fault in so many other ways. We'll just hope that the fault
    // occurs on the next thing to touch the stack, and doesn't happen in any
    // of the reads through `RBP` or stack pushes above ;-)
  }
  MOV_GPRv_GPRv_89(instr, XED_REG_RBP, temp_rbp);
  instr->decoded_pc = decoded_pc;
  AnalyzedStackUsage(instr, false, false);
}

// Mangle a `LEAVE` instruction. By making the `MOV RSP, RBP` explicit, we help
// the stack analysis in `granary/assemble/2_partition_fragments.cc` easier,
// and by making the `POP RBP` explicit, we make the next fragment get marked
// as having a valid stack.
static void MangleLeave(DecodedBasicBlock *block, Instruction *instr) {
  Instruction ni;
  auto decoded_pc = instr->decoded_pc;
  APP_NATIVE(MOV_GPRv_GPRv_89(&ni, XED_REG_RSP, XED_REG_RBP));
  block->UnsafeAppendInstruction(new AnnotationInstruction(IA_VALID_STACK));
  POP_GPRv_51(instr, XED_REG_RBP);
  instr->decoded_pc = decoded_pc;
  AnalyzedStackUsage(instr, true, true);
}

}  // namespace

// Perform "early" mangling of some instructions. This is primary to make the
// task of virtual register allocation tractable.
void MangleDecodedInstruction(DecodedBasicBlock *block, Instruction *instr,
                              bool rec) {
  // Do the stack usage "early" so that it is reflected in instructions
  // whose memory operands and split into intermediate `LEA` instructions.

  // Inject `AnnotationInstruction`s at opportune moments to make the job of
  // `granary/code/assemble/2_build_fragment_list.cc` easier by making sure
  // that if an instruction, e.g. `MOV RSP, [RAX]` modifies the stack pointer,
  // and that if it's converted to something like:
  //                LEA %0, [RAX];
  //                MOV RSP, [%0];
  // That both instructions (and therefore all related virtual registers)
  // appear in the same fragment partition during assembly.
  if (!rec && instr->WritesToStackPointer()) {
    if (instr->ShiftsStackPointer()) {
      if (XED_ICLASS_ADD != instr->iclass && XED_ICLASS_SUB != instr->iclass) {
        block->UnsafeAppendInstruction(
            new AnnotationInstruction(IA_VALID_STACK));
      }
    } else {
      block->UnsafeAppendInstruction(
          new AnnotationInstruction(IA_UNDEFINED_STACK));
    }
  }

  switch (instr->iclass) {
    case XED_ICLASS_CALL_NEAR:
    case XED_ICLASS_JMP:
      MangleIndirectCFI(block, instr);
      break;
    case XED_ICLASS_LEA:
      break;
    case XED_ICLASS_PUSH:
      ManglePushMemOp(block, instr);
      break;
    case XED_ICLASS_POP:
      ManglePop(block, instr);
      break;
    case XED_ICLASS_XLAT:
      MangleXLAT(block, instr);
      break;
    case XED_ICLASS_ENTER:
      MangleEnter(block, instr);
      break;
    case XED_ICLASS_LEAVE:
      MangleLeave(block, instr);
      break;
    default:
      MangleExplicitOps(block, instr);
      break;
  }
}

}  // namespace arch
}  // namespace granary
