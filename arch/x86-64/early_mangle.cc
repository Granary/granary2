/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/x86-64/builder.h"
#include "arch/x86-64/early_mangle.h"
#include "arch/x86-64/instruction.h"

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
    block->AppendInstruction(new NativeInstruction(&ni)); \
  } while (0)

// Append a "created" native instruction to the block.
#define APP_NATIVE(...) \
  do { \
    __VA_ARGS__; \
    ni.decoded_pc = instr->decoded_pc; \
    ni.effective_operand_width = instr->effective_operand_width; \
    ni.AnalyzeStackUsage(); \
    block->AppendInstruction(new NativeInstruction(&ni)); \
  } while (0)

// Append a "created" native instruction to the block, but mangle it first.
//
// Note: The recursive call to the mangler wil2l do the stack usage analysis.
#define APP_NATIVE_MANGLED(...) \
  do { \
    __VA_ARGS__; \
    ni.decoded_pc = instr->decoded_pc; \
    ni.effective_operand_width = instr->effective_operand_width; \
    MangleDecodedInstruction(block, &ni, true); \
    block->AppendInstruction(new NativeInstruction(&ni)); \
  } while (0)

namespace {

// Mangle an indirect CALL or JMP through memory into a MOV of the memory
// location into a virtual register, then an indirect call through the
// virtual register.
static void MangleIndirectCFI(DecodedBasicBlock *block, Instruction *instr) {
  if (instr->ops[0].IsMemory()) {
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
static void MangleExplicitMemOp(DecodedBasicBlock *block, Operand &op) {
  Instruction ni;

  // Special consideration is given to non-compound stack operands, e.g.
  // `MOV ..., [RSP]`. Because we might be changing the stack pointer, we
  // bring those operands out into their own instructions early on so that we
  // can potentially alter what the offset to them is later on (in the event
  // that virtual regs are spilled to the stack).
  if (!op.is_compound && !op.reg.IsStackPointer()) {
    return;
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
    if (XED_REG_INVALID != op.segment) {
      mem_reg.ConvertToSegmentOffset();
    }
    if (op.is_compound) {
      if (XED_REG_RSP == op.mem.reg_base) {
        mem_reg.ConvertToVirtualStackPointer();
      }
    } else {
      if (op.reg.IsStackPointer()) mem_reg.ConvertToVirtualStackPointer();
    }
    LEA_GPRv_AGEN(&ni, mem_reg, op);
    ni.ops[1].segment = XED_REG_INVALID;
    APP();

    op.is_compound = false;
    op.reg = mem_reg;
  }
}

// Mangle a `MOV_GPRv_GPRv_89 <gpr>, RSP` into an `LEA_GPRv_AGEN <gpr>, [RSP]`.
// This plays nicer with later slot allocation.
static void MoveStackPointerToGPR(Instruction *instr) {
  auto decoded_pc = instr->decoded_pc;
  LEA_GPRv_AGEN(instr, instr->ops[0].reg,
                BaseDispMemOp(0, XED_REG_RSP, arch::ADDRESS_WIDTH_BITS));
  instr->decoded_pc = decoded_pc;
}

// Add in an extra instruction for a read from the stack pointer. The purpose
// of this is that if an instruction reads from the stack pointer, then we'll
// potentially need to emulate what the intended stack pointer read is later
// on when virtual register spilling might have changed the actual stack
// pointer.
static void MangleExplicitStackPointerRegOp(DecodedBasicBlock *block,
                                            Instruction *instr,
                                            Operand &op) {

  // We special case `MOV_GPRv_GPRv <reg>, RSP` so that later we can potentially
  // avoid virtual register usage on function prologues.
  if (!op.IsWrite()) {
    if (XED_IFORM_MOV_GPRv_GPRv_89 == instr->iform &&
        arch::GPR_WIDTH_BITS == instr->effective_operand_width) {
      MoveStackPointerToGPR(instr);
      if (REDZONE_SIZE_BYTES) {
        block->AppendInstruction(
            new AnnotationInstruction(IA_UNKNOWN_STACK_BELOW));
      }
    } else if (XED_ICLASS_LEA == instr->iclass) {
      return;  // Mangling would be redundant.
    } else {
      Instruction ni;
      auto sp = block->AllocateVirtualRegister();
      sp.ConvertToVirtualStackPointer();
      APP(LEA_GPRv_AGEN(&ni, sp, BaseDispMemOp(0, XED_REG_RSP,
                                               arch::ADDRESS_WIDTH_BITS)));
      sp.Widen(op.reg.ByteWidth());
      op.reg = sp;  // Replace the operand.
    }
  }
}

static void MangleSegmentOffset(DecodedBasicBlock *block, Operand &op) {
  Instruction ni;
  auto offset = block->AllocateVirtualRegister();
  offset.ConvertToSegmentOffset();

  MOV_GPRv_IMMv(&ni, offset.WidenedTo(4 /* bytes */),
                static_cast<uint32_t>(op.addr.as_uint));
  ni.effective_operand_width = 32;
  ni.ops[1].width = 32;
  APP();

  op.type = XED_ENCODER_OPERAND_TYPE_MEM;
  op.reg = offset;
}

// Mangle an explicit memory operand. This will expand memory operands into
// `LEA` instructions.
static void MangleExplicitOps(DecodedBasicBlock *block, Instruction *instr) {
  Instruction ni;
  auto unmangled_uses_sp = instr->ReadsFromStackPointer() ||
                           instr->WritesToStackPointer();
  GRANARY_ASSERT(XED_ICLASS_LEA != instr->iclass);

  for (auto &op : instr->ops) {
    if (op.is_explicit) {
      if (XED_ENCODER_OPERAND_TYPE_MEM == op.type) {
        MangleExplicitMemOp(block, op);
      } else if (XED_ENCODER_OPERAND_TYPE_PTR == op.type) {
        if (XED_REG_INVALID != op.segment && XED_REG_DS != op.segment) {
          MangleSegmentOffset(block, op);
        }
      } else if (op.IsRegister() && op.reg.IsStackPointer()) {
        MangleExplicitStackPointerRegOp(block, instr, op);
      }
    }
  }

  // Re-analyze this instruction so that we don't later report that some
  // instruction is operating on the stack when it isn't.
  if (unmangled_uses_sp) {
    instr->AnalyzeStackUsage();
  }
}

// Mark an instruction as potentially reading and writing to the stack.
static void AnalyzedStackUsage(Instruction *instr, bool does_read,
                               bool does_write) {
  instr->analyzed_stack_usage = true;
  instr->reads_from_stack_pointer = does_read;
  instr->writes_to_stack_pointer = does_write;
}

// Mangle a `PUSH_MEMv` instruction. We have to mangle this because otherwise
// we might hit the situation where we need to do a memory-to-memory move, but
// can't pull it off with just a `MOV`.
static void ManglePushMemOp(DecodedBasicBlock *block, Instruction *instr) {
  GRANARY_ASSERT(-1 != instr->effective_operand_width);
  auto op = instr->ops[0];
  auto stack_shift = instr->effective_operand_width / 8;
  auto vr = block->AllocateVirtualRegister(stack_shift);
  Instruction ni;
  APP_NATIVE_MANGLED(MOV_GPRv_MEMv(&ni, vr, op));
  instr->iform = XED_IFORM_PUSH_GPRv_50;
  instr->ops[0].reg = vr;
  instr->ops[0].type = XED_ENCODER_OPERAND_TYPE_REG;
}

// Mangle `PUSH_IMMz` and `PUSH_IMMb` instructions.
//
// TODO(pag): This type of mangling should be moved to a "late" mangling phase
//            that happens after the validity of fragment stacks has been
//            identified, otherwise this penalizes fragments on valid stacks.
//
//            One suitable place would be `ManglePush` in `9_allocate_slots.cc`.
//
// Note: During decoding, we will have done the correct sign-extension.
static void ManglePushImmOp(DecodedBasicBlock *block, Instruction *instr) {
  auto op = instr->ops[0];
  auto vr = block->AllocateVirtualRegister(op.ByteWidth());
  Instruction ni;
  APP(MOV_GPRv_IMMv(&ni, vr, op));
  instr->iform = XED_IFORM_PUSH_GPRv_50;
  instr->ops[0].reg = vr;
  instr->ops[0].type = XED_ENCODER_OPERAND_TYPE_REG;
}

// Mangle a `PUSH_FS` or `PUSH_GS` instruction. We do this mangling ahead of
// time and not during virtual register slot mangling (assembly step 9) because
// it's convenient.
//
// Note: Need to do the proper zero-extension of the 16 bit value.
static void ManglePushSegReg(DecodedBasicBlock *block, Instruction *instr) {
  Instruction ni;
  auto vr_16 = block->AllocateVirtualRegister(2);
  auto vr_32 = vr_16.WidenedTo(4);
  APP(MOV_GPRv_SEG(&ni, vr_16, instr->ops[0].reg));
  APP(MOVZX_GPRv_GPR16(&ni, vr_32, vr_16));
  auto stack_shift = instr->effective_operand_width / 8;
  instr->iform = XED_IFORM_PUSH_GPRv_50;
  instr->ops[0].reg = vr_16.WidenedTo(stack_shift);
  instr->ops[0].width = instr->effective_operand_width;
  instr->ops[0].is_sticky = false;
}

// Mangle a `PUSH_*` instruction.
static void ManglePush(DecodedBasicBlock *block, Instruction *instr) {
  if (instr->ops[0].IsMemory()) {
    ManglePushMemOp(block, instr);
  } else if (instr->ops[0].IsImmediate()) {
    if (false) ManglePushImmOp(block, instr);  // Disabled for now.
  } else if (XED_IFORM_PUSH_FS == instr->iform ||
             XED_IFORM_PUSH_GS == instr->iform) {
    ManglePushSegReg(block, instr);
  }
}

// Mangle a `POP_MEMv` instruction.
static void ManglePopMemOp(DecodedBasicBlock *block, Instruction *instr) {
  auto op = instr->ops[0];
  Instruction ni;
  GRANARY_ASSERT(-1 != instr->effective_operand_width);
  auto stack_shift = instr->effective_operand_width / 8;
  auto vr = block->AllocateVirtualRegister(stack_shift);
  auto stack_mem_op = BaseDispMemOp(0, XED_REG_RSP,
                                    instr->effective_operand_width);
  APP(MOV_GPRv_MEMv(&ni, vr, stack_mem_op));
  if (op.is_compound) {
    if (XED_REG_RSP == op.mem.reg_base || XED_REG_ESP == op.mem.reg_base) {
      op.mem.disp += stack_shift;
    }
  } else if (op.reg.IsStackPointer()) {
    op = BaseDispMemOp(stack_shift, XED_REG_RSP, op.width);
  }
  APP_NATIVE_MANGLED(MOV_MEMv_GPRv(&ni, op, vr));
  LEA_GPRv_AGEN(instr, XED_REG_RSP, BaseDispMemOp(stack_shift, XED_REG_RSP,
                                                  arch::ADDRESS_WIDTH_BITS));
  AnalyzedStackUsage(instr, true, true);
}

// Mangle a `POP_GPRv` instruction, where the popped GPR is the stack pointer.
static void ManglePopStackPointer(DecodedBasicBlock *block,
                                  Instruction *instr) {
  GRANARY_ASSERT(-1 != instr->effective_operand_width);
  auto decoded_pc = instr->decoded_pc;
  auto op_size = instr->effective_operand_width;
  auto stack_mem_op = BaseDispMemOp(0, XED_REG_RSP,
                                    instr->effective_operand_width);
  MOV_GPRv_MEMv(instr, instr->ops[0].reg, stack_mem_op);
  instr->decoded_pc = decoded_pc;
  instr->effective_operand_width = op_size;
  AnalyzedStackUsage(instr, true, true);
  MangleDecodedInstruction(block, instr, true);
}

// Mangle a `POP_FS` or `POP_GS` instruction. We do this mangling ahead of
// time and not during virtual register slot mangling (assembly step 9) because
// it's convenient.
//
// Note: Need to do the proper zero-extension of the 16 bit value.
static void ManglePopSegReg(DecodedBasicBlock *block, Instruction *instr) {
  Instruction ni;
  auto stack_shift = instr->effective_operand_width / 8;
  auto vr = block->AllocateVirtualRegister(stack_shift);
  auto vr_16 = vr.WidenedTo(2);
  auto seg_reg = instr->ops[0].reg;

  // Pop into a GPR instead of into the segment.
  instr->ops[0].reg = vr;
  instr->ops[0].width = instr->effective_operand_width;
  instr->ops[0].is_sticky = false;
  instr->iform = XED_IFORM_POP_GPRv_51;
  block->AppendInstruction(new NativeInstruction(instr));

  // Replace `instr` with a `MOV` into the segment reg with the value that was
  // popped off the top of the stack.
  auto decoded_pc = instr->decoded_pc;
  MOV_SEG_GPR16(instr, seg_reg, vr_16);
  instr->decoded_pc = decoded_pc;
  instr->effective_operand_width = 16;
}

// Mangle a `POP_*` instruction.
static void ManglePop(DecodedBasicBlock *block, Instruction *instr) {
  auto op = instr->ops[0];
  if (op.IsMemory()) {
    ManglePopMemOp(block, instr);
  } else if (op.IsRegister() && op.reg.IsStackPointer()) {
    ManglePopStackPointer(block, instr);
  } else if (XED_IFORM_POP_FS == instr->iform ||
             XED_IFORM_POP_GS == instr->iform) {
    ManglePopSegReg(block, instr);
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
  instr->ops[1].width = 8;  // Bits.
}

// Mangle an `ENTER` instruction.
static void MangleEnter(DecodedBasicBlock *block, Instruction *instr) {
  Instruction ni;
  auto frame_size = instr->ops[0].imm.as_uint & 0xFFFFUL;
  auto num_args = instr->ops[1].imm.as_uint & 0x1FUL;
  auto temp_rbp = block->AllocateVirtualRegister();
  auto decoded_pc = instr->decoded_pc;
  temp_rbp.ConvertToVirtualStackPointer();

  APP_NATIVE(
      PUSH_GPRv_50(&ni, XED_REG_RBP);
      ni.effective_operand_width = arch::GPR_WIDTH_BITS; );
  APP_NATIVE(LEA_GPRv_AGEN(
      &ni, temp_rbp, BaseDispMemOp(0, XED_REG_RSP, arch::ADDRESS_WIDTH_BITS)));

  if (num_args) {
    for (auto i = 1UL; i < num_args; ++i) {
      auto offset = -static_cast<int32_t>(i * arch::ADDRESS_WIDTH_BYTES);

      // In the case of something like watchpoints, where `RBP` is being
      // tracked, and where the application is doing something funky with
      // `RBP` (e.g. it's somehow watched), then we want to see these memory
      // writes.
      APP_NATIVE_MANGLED(
          PUSH_MEMv(&ni, BaseDispMemOp(offset, XED_REG_RBP,
                                       arch::GPR_WIDTH_BITS));
          ni.effective_operand_width = arch::GPR_WIDTH_BITS; );
    }
    APP_NATIVE(
        PUSH_GPRv_50(&ni, temp_rbp);
        ni.effective_operand_width = arch::GPR_WIDTH_BITS; );
  }

  if (frame_size) {
    APP(LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                      BaseDispMemOp(-static_cast<int32_t>(frame_size),
                                    XED_REG_RSP,
                                    arch::ADDRESS_WIDTH_BITS)));

    // Enter finishes with a memory write that is "unused". This is to detect
    // stack segment issues and page faults. We don't even bother with this
    // because emulating the exception behavior of `ENTER` is pointless because
    // it could fault in so many other ways. We'll just hope that the fault
    // occurs on the next thing to touch the stack, and doesn't happen in any
    // of the reads through `RBP` or stack pushes above ;-)
  }
  MOV_GPRv_GPRv_89(instr, XED_REG_RBP, temp_rbp);
  instr->decoded_pc = decoded_pc;
  instr->effective_operand_width = arch::GPR_WIDTH_BITS;
  AnalyzedStackUsage(instr, false, false);
}

// Mangle a `LEAVE` instruction. By making the `MOV RSP <- RBP` explicit, we help
// the stack analysis in `granary/assemble/2_partition_fragments.cc` easier,
// and by making the `POP RBP` explicit, we make the next fragment get marked
// as having a valid stack.
static void MangleLeave(DecodedBasicBlock *block, Instruction *instr) {
  Instruction ni;
  auto decoded_pc = instr->decoded_pc;
  APP_NATIVE(MOV_GPRv_GPRv_89(&ni, XED_REG_RSP, XED_REG_RBP));
  block->AppendInstruction(new AnnotationInstruction(IA_VALID_STACK));
  POP_GPRv_51(instr, XED_REG_RBP);
  instr->decoded_pc = decoded_pc;
  instr->effective_operand_width = arch::GPR_WIDTH_BITS;
  AnalyzedStackUsage(instr, true, true);
}

// This is a big hack: it is our way of ensuring that during late mangling, we
// have access to some kind of virtual register for `PUSHF` and `PUSHFQ`.
static void ManglePushFlags(DecodedBasicBlock *block, Instruction *instr) {
  auto flag_size = instr->effective_operand_width / 8;
  instr->ops[0].type = XED_ENCODER_OPERAND_TYPE_REG;
  instr->ops[0].reg = block->AllocateVirtualRegister(flag_size);
  instr->ops[0].rw = XED_OPERAND_ACTION_W;
  instr->ops[0].width = instr->effective_operand_width;

  // Note: Need to mark it as explicit so that it will correctly be replaced
  //       when the register scheduler gets around to scheduling the reg.
  instr->ops[0].is_explicit = true;
  instr->ops[0].is_sticky = false;
  ++(instr->num_explicit_ops);
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
        block->AppendInstruction(
            new AnnotationInstruction(IA_VALID_STACK));
      }
    } else {
      switch (instr->iclass) {
        // These instruction's don't shift the stack pointer by a constant
        // amount, but still signal that it's valid.
        case XED_ICLASS_RET_FAR:
        case XED_ICLASS_CALL_FAR:
        case XED_ICLASS_IRET:
          block->AppendInstruction(
             new AnnotationInstruction(IA_VALID_STACK));
          break;

        // An instruction like `LEAVE` is first caught here, then later mangled
        // so that the end result is:
        //      `<unknown stack>; MOV RSP, RBP; <valid stack>; POP RBP`
        default:
          block->AppendInstruction(
              new AnnotationInstruction(IA_INVALID_STACK));
      }
    }
  }
  switch (instr->iclass) {
    case XED_ICLASS_CALL_NEAR:
    case XED_ICLASS_JMP:
      MangleIndirectCFI(block, instr);
      break;
    case XED_ICLASS_LEA:
      if (REDZONE_SIZE_BYTES && instr->ReadsFromStackPointer()) {
        // The application copies the stack pointer into a register. After
        // this point the copied stack pointer might be used to access
        // memory in the redzone.
        block->AppendInstruction(
            new AnnotationInstruction(IA_UNKNOWN_STACK_BELOW));
      }
      break;
    case XED_ICLASS_PUSH:
      ManglePush(block, instr);
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
    case XED_ICLASS_PUSHF:
    case XED_ICLASS_PUSHFQ:
      ManglePushFlags(block, instr);
      break;

    // Note: Don't need to do any early mangling for `POPF` or `POPFQ` as we
    //       late mangle them into a `PUSH [RSP + offset]; POPF`.
    case XED_ICLASS_POPF:
    case XED_ICLASS_POPFQ:
    case XED_ICLASS_CLI:
    case XED_ICLASS_STI:
    case XED_ICLASS_WRMSR:
    case XED_ICLASS_FWAIT:
      block->AppendInstruction(
          new AnnotationInstruction(IA_CHANGES_INTERRUPT_STATE));
      break;

    default:
      MangleExplicitOps(block, instr);
      break;
  }
}

}  // namespace arch
}  // namespace granary
