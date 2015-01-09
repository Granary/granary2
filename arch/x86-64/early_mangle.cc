/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/early_mangle.h"
#include "arch/x86-64/builder.h"
#include "arch/x86-64/instruction.h"

#include "granary/base/base.h"

#include "granary/cfg/block.h"
#include "granary/cfg/instruction.h"

#include "granary/breakpoint.h"

namespace granary {
namespace arch {

// Append a non-native, created instruction to the block.
#define APP(...) \
  do { \
    __VA_ARGS__; \
    mangler->block->AppendInstruction(new NativeInstruction(&ni)); \
  } while (0)

// Append a "created" native instruction to the block.
#define APP_NATIVE(...) \
  do { \
    __VA_ARGS__; \
    ni.decoded_pc = instr->decoded_pc; \
    ni.effective_operand_width = instr->effective_operand_width; \
    mangler->block->AppendInstruction(new NativeInstruction(&ni)); \
  } while (0)

// Append a "created" native instruction to the block, but mangle it first.
//
// Note: The recursive call to the mangler wil2l do the stack usage analysis.
#define APP_NATIVE_MANGLED(...) \
  do { \
    __VA_ARGS__; \
    ni.decoded_pc = instr->decoded_pc; \
    ni.effective_operand_width = instr->effective_operand_width; \
    mangler->MangleDecodedInstruction(&ni, true); \
    mangler->block->AppendInstruction(new NativeInstruction(&ni)); \
  } while (0)

namespace {

// Mangle an indirect CALL or JMP. This ensures that all indirect control flow
// uses a virtual register.
static void MangleIndirectCFI(EarlyMangler *mangler, Instruction *instr) {
  Instruction ni;
  auto &aop(instr->ops[0]);
  auto target_loc = mangler->AllocateVirtualRegister();
  if (aop.IsMemory()) {
    APP_NATIVE_MANGLED(MOV_GPRv_MEMv(&ni, target_loc, aop));
    aop.type = XED_ENCODER_OPERAND_TYPE_REG;
    aop.reg = target_loc;
    aop.is_sticky = false;
    aop.is_compound = false;
    aop.segment = XED_REG_INVALID;

  } else if (aop.IsRegister() && !aop.reg.IsVirtual()) {
    APP(MOV_GPRv_GPRv_89(&ni, target_loc, aop.reg));
    aop.reg = target_loc;
  }
}

// Mangle an explicit memory operand. This will expand memory operands into
// `LEA` instructions. The motivation for this is that ideally, we want
// instrumentation tools to be able to always deal with memory addresses as
// either: 1) registers, 2) absolute addresses, or 3) offsets from a segment.
//
// Note: This applies to `XED_ENCODER_OPERAND_TYPE_MEM` only.
static void MangleExplicitMemOp(EarlyMangler *mangler, Operand &op) {
  // Special consideration is given to non-compound stack operands, e.g.
  // `MOV ..., [RSP]`. Because we might be changing the stack pointer, we
  // bring those operands out into their own instructions early on so that we
  // can potentially alter what the offset to them is later on (in the event
  // that virtual regs are spilled to the stack).
  if (!op.is_compound && !op.reg.IsStackPointer()) return;

  // All built-in memory operands, other than `XLAT`, a simple dereferences
  // of a single base register. We will convert most into non-compound
  // operands to make them easier to manipulate from the instrumentation
  // side of things.
  if (op.is_sticky) {
    if (0 == op.mem.disp && !op.mem.index.IsValid()) {
      GRANARY_ASSERT(!op.mem.base.IsStackPointer());
      op.is_compound = false;
      op.reg = op.mem.base;
    }
  } else {
    // If it's not a compound memory operand, then don't split it apart.
    if (!op.is_compound) {
      return;
    }

    auto mem_reg = mangler->AllocateVirtualRegister();
    if (op.mem.base.IsStackPointer()) mem_reg.MarkAsStackPointerAlias();

    Instruction ni;
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
  auto reg = instr->ops[0].reg;
  reg.MarkAsStackPointerAlias();
  LEA_GPRv_AGEN(instr, reg,
                BaseDispMemOp(0, XED_REG_RSP, arch::ADDRESS_WIDTH_BITS));
  instr->decoded_pc = decoded_pc;
}

// Add in an extra instruction for a read from the stack pointer. The purpose
// of this is that if an instruction reads from the stack pointer, then we'll
// potentially need to emulate what the intended stack pointer read is later
// on when virtual register spilling might have changed the actual stack
// pointer.
static void MangleExplicitStackPointerRegOp(EarlyMangler *mangler,
                                            Instruction *instr,
                                            Operand &op) {

  // We special case `MOV_GPRv_GPRv <reg>, RSP` so that later we can potentially
  // avoid virtual register usage on function prologues.
  if (!op.IsWrite()) {
    if (XED_IFORM_MOV_GPRv_GPRv_89 == instr->iform &&
        arch::GPR_WIDTH_BITS == instr->effective_operand_width) {
      MoveStackPointerToGPR(instr);
    } else if (XED_ICLASS_LEA == instr->iclass) {
      return;  // Mangling would be redundant.
    } else {
      Instruction ni;
      auto sp = mangler->AllocateVirtualRegister();
      sp.MarkAsStackPointerAlias();
      APP(LEA_GPRv_AGEN(&ni, sp, BaseDispMemOp(0, XED_REG_RSP,
                                               arch::ADDRESS_WIDTH_BITS)));
      sp.Widen(op.reg.ByteWidth());
      op.reg = sp;  // Replace the operand.
    }
  }
}

// Note: This applies to `XED_ENCODER_OPERAND_TYPE_PTR` only.
static void MangleSegmentOffset(EarlyMangler *mangler, Operand &op) {
  Instruction ni;
  auto offset = mangler->AllocateVirtualRegister();
  APP(MOV_GPRv_IMMv(&ni, offset, op.addr.as_uint));
  op.type = XED_ENCODER_OPERAND_TYPE_MEM;
  op.is_compound = false;
  op.reg = offset;
}

// Mangle an explicit memory operand. This will expand memory operands into
// `LEA` instructions.
static void MangleExplicitOps(EarlyMangler *mangler, Instruction *instr) {
  Instruction ni;
  auto unmangled_uses_sp = instr->ReadsFromStackPointer() ||
                           instr->WritesToStackPointer();
  GRANARY_ASSERT(XED_ICLASS_LEA != instr->iclass);

  for (uint16_t i = 0; i < instr->num_explicit_ops; ++i) {
    auto &op(instr->ops[i]);
    GRANARY_ASSERT(op.is_explicit);

    if (XED_ENCODER_OPERAND_TYPE_MEM == op.type) {
      MangleExplicitMemOp(mangler, op);

    } else if (XED_ENCODER_OPERAND_TYPE_PTR == op.type) {
      if (XED_REG_INVALID != op.segment && XED_REG_DS != op.segment) {
        MangleSegmentOffset(mangler, op);
      }

    } else if (op.IsRegister() && op.reg.IsStackPointer()) {
      MangleExplicitStackPointerRegOp(mangler, instr, op);
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
static void ManglePushMemOp(EarlyMangler *mangler, Instruction *instr) {
  GRANARY_ASSERT(0 != instr->effective_operand_width);
  auto op = instr->ops[0];
  size_t stack_shift = instr->effective_operand_width / 8UL;
  auto vr = mangler->AllocateVirtualRegister(stack_shift);
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
static void ManglePushImmOp(EarlyMangler *mangler, Instruction *instr) {
  auto op = instr->ops[0];
  auto vr = mangler->AllocateVirtualRegister(op.ByteWidth());
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
static void ManglePushSegReg(EarlyMangler *mangler, Instruction *instr) {
  Instruction ni;
  auto vr_16 = mangler->AllocateVirtualRegister(2);
  auto vr_32 = vr_16.WidenedTo(4);
  APP(MOV_GPRv_SEG(&ni, vr_16, instr->ops[0].reg));
  APP(MOVZX_GPRv_GPR16(&ni, vr_32, vr_16));
  size_t stack_shift = instr->effective_operand_width / 8UL;
  instr->iform = XED_IFORM_PUSH_GPRv_50;
  instr->ops[0].reg = vr_16.WidenedTo(stack_shift);
  instr->ops[0].width = instr->effective_operand_width;
  instr->ops[0].is_sticky = false;
}

// Mangle a `PUSH_*` instruction.
static void ManglePush(EarlyMangler *mangler, Instruction *instr) {
  if (instr->ops[0].IsMemory()) {
    ManglePushMemOp(mangler, instr);
  } else if (instr->ops[0].IsImmediate()) {
    if (false) ManglePushImmOp(mangler, instr);  // Disabled for now.
  } else if (XED_IFORM_PUSH_FS == instr->iform ||
             XED_IFORM_PUSH_GS == instr->iform) {
    ManglePushSegReg(mangler, instr);
  }
}

// Mangle a `POP_MEMv` instruction.
static void ManglePopMemOp(EarlyMangler *mangler, Instruction *instr) {
  GRANARY_ASSERT(0 < instr->effective_operand_width);
  size_t stack_shift = instr->effective_operand_width / arch::BYTE_WIDTH_BITS;
  auto stack_shift_int32 = static_cast<int32_t>(stack_shift);
  GRANARY_ASSERT(instr->StackPointerShiftAmount() == stack_shift_int32);

  Instruction ni;
  auto op = instr->ops[0];
  auto vr = mangler->AllocateVirtualRegister(stack_shift);
  auto stack_mem_op = BaseDispMemOp(0, XED_REG_RSP,
                                    instr->effective_operand_width);
  APP(MOV_GPRv_MEMv(&ni, vr, stack_mem_op));
  if (op.IsPointer()) {
    // Nothing.
  } else if (op.is_compound) {
    if (op.mem.base.IsStackPointer()) {
      GRANARY_ASSERT(arch::ADDRESS_WIDTH_BITS == op.mem.base.BitWidth());
      op.mem.disp += stack_shift;
    }
  } else if (op.reg.IsStackPointer()) {
    GRANARY_ASSERT(arch::ADDRESS_WIDTH_BITS == op.reg.BitWidth());
    op = BaseDispMemOp(stack_shift_int32, XED_REG_RSP, op.width);
  }
  APP_NATIVE_MANGLED(MOV_MEMv_GPRv(&ni, op, vr));
  LEA_GPRv_AGEN(instr, XED_REG_RSP, BaseDispMemOp(stack_shift_int32,
                                                  XED_REG_RSP,
                                                  arch::ADDRESS_WIDTH_BITS));
  AnalyzedStackUsage(instr, true, true);
}

// Mangle a `POP_GPRv` instruction, where the popped GPR is the stack pointer.
static void ManglePopStackPointer(EarlyMangler *mangler,
                                  Instruction *instr) {
  GRANARY_ASSERT(0 < instr->effective_operand_width);
  auto decoded_pc = instr->decoded_pc;
  auto op_size = instr->effective_operand_width;
  auto stack_mem_op = BaseDispMemOp(0, XED_REG_RSP,
                                    instr->effective_operand_width);
  MOV_GPRv_MEMv(instr, instr->ops[0].reg, stack_mem_op);
  instr->decoded_pc = decoded_pc;
  instr->effective_operand_width = op_size;
  AnalyzedStackUsage(instr, true, true);
  mangler->MangleDecodedInstruction(instr, true);
}

// Mangle a `POP_FS` or `POP_GS` instruction. We do this mangling ahead of
// time and not during virtual register slot mangling (assembly step 9) because
// it's convenient.
//
// Note: Need to do the proper zero-extension of the 16 bit value.
static void ManglePopSegReg(EarlyMangler *mangler, Instruction *instr) {
  GRANARY_ASSERT(0 < instr->effective_operand_width);
  size_t stack_shift = instr->effective_operand_width / 8UL;
  GRANARY_ASSERT(instr->StackPointerShiftAmount() ==
                 static_cast<int>(stack_shift));

  Instruction ni;
  auto vr = mangler->AllocateVirtualRegister(stack_shift);
  auto vr_16 = vr.WidenedTo(2);
  auto seg_reg = instr->ops[0].reg;

  // Pop into a GPR instead of into the segment.
  instr->ops[0].reg = vr;
  instr->ops[0].width = instr->effective_operand_width;
  instr->ops[0].is_sticky = false;
  instr->iform = XED_IFORM_POP_GPRv_51;
  mangler->block->AppendInstruction(new NativeInstruction(instr));

  // Replace `instr` with a `MOV` into the segment reg with the value that was
  // popped off the top of the stack.
  auto decoded_pc = instr->decoded_pc;
  MOV_SEG_GPR16(instr, seg_reg, vr_16);
  instr->decoded_pc = decoded_pc;
  instr->effective_operand_width = 16;
}

// Mangle a `POP_*` instruction.
static void ManglePop(EarlyMangler *mangler, Instruction *instr) {
  auto op = instr->ops[0];
  if (op.IsMemory()) {
    ManglePopMemOp(mangler, instr);
  } else if (op.IsRegister() && op.reg.IsStackPointer()) {
    ManglePopStackPointer(mangler, instr);
  } else if (XED_IFORM_POP_FS == instr->iform ||
             XED_IFORM_POP_GS == instr->iform) {
    ManglePopSegReg(mangler, instr);
  }
}

// Mangle an `XLAT` instruction to use virtual registers. This is to avoid the
// issue where `XLAT` is really the only instruction where two differently sized
// registers are used as a base and index register.
static void MangleXLAT(EarlyMangler *mangler, Instruction *instr) {
  Instruction ni;
  auto addr = mangler->AllocateVirtualRegister();
  auto decoded_pc = instr->decoded_pc;
  APP(MOVZX_GPRv_GPR8(&ni, addr, XED_REG_AL));
  APP(LEA_GPRv_AGEN(&ni, addr, addr, XED_REG_RBX));
  MOV_GPR8_MEMb(instr, XED_REG_AL, addr);
  instr->decoded_pc = decoded_pc;
  instr->ops[1].width = 8;  // Bits.
}

// Mangle an `ENTER` instruction.
static void MangleEnter(EarlyMangler *mangler, Instruction *instr) {
  Instruction ni;
  auto frame_size = instr->ops[0].imm.as_uint & 0xFFFFUL;
  auto num_args = instr->ops[1].imm.as_uint & 0x1FUL;
  auto temp_rbp = mangler->AllocateVirtualRegister();
  auto decoded_pc = instr->decoded_pc;
  temp_rbp.MarkAsStackPointerAlias();

  APP_NATIVE(
      PUSH_GPRv_50(&ni, XED_REG_RBP);
      ni.effective_operand_width = arch::GPR_WIDTH_BITS; );
  APP_NATIVE(LEA_GPRv_AGEN(
      &ni, temp_rbp, BaseDispMemOp(0, XED_REG_RSP, arch::ADDRESS_WIDTH_BITS)));

  if (num_args) {
    auto disp = -static_cast<int>(num_args - 1) * arch::ADDRESS_WIDTH_BYTES;
    APP_NATIVE(LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                             BaseDispMemOp(disp, XED_REG_RSP,
                                           arch::ADDRESS_WIDTH_BITS)));

    auto copied_addr = mangler->AllocateVirtualRegister();
    auto dest_addr = mangler->AllocateVirtualRegister();
    auto copied_val = mangler->AllocateVirtualRegister();

    dest_addr.MarkAsStackPointerAlias();

    for (auto i = 1UL; i < num_args; ++i) {
      auto rbp_disp = -static_cast<int32_t>(i * arch::ADDRESS_WIDTH_BYTES);
      auto rsp_disp = static_cast<int32_t>((num_args - i - 1) *
                                           arch::ADDRESS_WIDTH_BYTES);
      // In the case of something like watchpoints, where `RBP` is being
      // tracked, and where the application is doing something funky with
      // `RBP` (e.g. it's somehow watched), then we want to see these memory
      // writes.
      APP(LEA_GPRv_AGEN(&ni, copied_addr, BaseDispMemOp(rbp_disp, XED_REG_RBP,
                                                        arch::GPR_WIDTH_BITS)));
      APP(LEA_GPRv_AGEN(&ni, dest_addr, BaseDispMemOp(rsp_disp, XED_REG_RSP,
                                                      arch::GPR_WIDTH_BITS)));
      APP_NATIVE(MOV_GPRv_MEMv(&ni, copied_val,
                               BaseDispMemOp(0, copied_addr,
                                             arch::GPR_WIDTH_BITS)));
      APP_NATIVE(MOV_MEMv_GPRv(&ni, BaseDispMemOp(0, dest_addr,
                                                  arch::GPR_WIDTH_BITS),
                               copied_val));
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
static void MangleLeave(EarlyMangler *mangler, Instruction *instr) {
  Instruction ni;
  auto decoded_pc = instr->decoded_pc;
  APP_NATIVE(MOV_GPRv_GPRv_89(&ni, XED_REG_RSP, XED_REG_RBP));
  POP_GPRv_51(instr, XED_REG_RBP);
  instr->decoded_pc = decoded_pc;
  instr->effective_operand_width = arch::GPR_WIDTH_BITS;
  AnalyzedStackUsage(instr, true, true);
}

// This is a big hack: it is our way of ensuring that during late mangling, we
// have access to some kind of virtual register for `PUSHF` and `PUSHFQ`.
static void ManglePushFlags(EarlyMangler *mangler, Instruction *instr) {
  instr->ops[0].type = XED_ENCODER_OPERAND_TYPE_REG;
  instr->ops[0].reg = mangler->AllocateVirtualRegister();
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
void EarlyMangler::MangleDecodedInstruction(Instruction *instr,
                                            bool rec) {
  // Reset the mangler. This should be called between distinct native
  // instructions, where VR re-usage shouldn't interfere.
  if (!rec) {
    reg_num = 0;
  }

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
  if (!rec && instr->WritesToStackPointer() && !instr->ShiftsStackPointer()) {
    switch (instr->iclass) {
      // These instruction's don't shift the stack pointer by a constant
      // amount, but still signal that it's valid.
      case XED_ICLASS_RET_FAR:
      case XED_ICLASS_CALL_FAR:
      case XED_ICLASS_IRET:
        break;

      // An instruction like `LEAVE` is first caught here, then later mangled
      // so that the end result is:
      //      `<unknown stack>; MOV RSP, RBP; <valid stack>; POP RBP`
      default:
        block->AppendInstruction(
            new AnnotationInstruction(kAnnotInvalidStack));
    }
  }
  switch (instr->iclass) {
    case XED_ICLASS_CALL_NEAR:
    case XED_ICLASS_JMP:
      MangleIndirectCFI(this, instr);
      break;
    case XED_ICLASS_PUSH:
      ManglePush(this, instr);
      break;
    case XED_ICLASS_POP:
      ManglePop(this, instr);
      break;
    case XED_ICLASS_XLAT:
      MangleXLAT(this, instr);
      break;
    case XED_ICLASS_ENTER:
      MangleEnter(this, instr);
      break;
    case XED_ICLASS_LEAVE:
      MangleLeave(this, instr);
      break;
    case XED_ICLASS_PUSHF:
    case XED_ICLASS_PUSHFQ:
      ManglePushFlags(this, instr);
      break;

    // Note: Don't need to do any early mangling for `POPF` or `POPFQ` as we
    //       late mangle them into a `PUSH [RSP + offset]; POPF`.
    case XED_ICLASS_POPF:
    case XED_ICLASS_POPFQ:
    case XED_ICLASS_CLI:
    case XED_ICLASS_STI:
      block->AppendInstruction(
          new AnnotationInstruction(kAnnotInterruptDeliveryStateChange));
      break;

    case XED_ICLASS_LEA:
      if (instr->ReadsFromStackPointer()) {
        instr->ops[0].reg.MarkAsStackPointerAlias();
      }
      break;

    default:
      MangleExplicitOps(this, instr);
      break;
  }
}

}  // namespace arch
}  // namespace granary
