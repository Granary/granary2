/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/x86-64/builder.h"
#include "arch/x86-64/slot.h"

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"

#include "granary/cache.h"

#define APP(...) \
  do { \
    __VA_ARGS__ ; \
    instrs->Append(new NativeInstruction(&ni)); \
  } while (0)

#define BEFORE(...) \
  do { \
    __VA_ARGS__ ; \
    instrs->InsertBefore(instr, new NativeInstruction(&ni)); \
  } while (0)

namespace granary {
namespace arch {

// Inserts code before `instr` in `instrs` to switch off of the native stack.
//
// Note: This function has an architecture-specific implementation.
void SwitchOffStack(InstructionList *instrs, granary::Instruction *instr) {
  if (REDZONE_SIZE_BYTES) {
    Instruction ni;
    BEFORE(SHIFT_REDZONE(&ni);
           ni.is_stack_blind = true;
           ni.analyzed_stack_usage = false; );
  } else {
    // TODO(pag): Should we do stack switching here??
    granary_curiosity();
  }
}

// Inserts code before `instr` in `instrs` to switch on to the native stack.
//
// Note: This function has an architecture-specific implementation.
void SwitchOnStack(InstructionList *instrs, granary::Instruction *instr) {
  if (REDZONE_SIZE_BYTES) {
    Instruction ni;
    BEFORE(UNSHIFT_REDZONE(&ni);
           ni.is_stack_blind = true;
           ni.analyzed_stack_usage = false; );
  } else {
    // TODO(pag): Should we do stack switching here??
    granary_curiosity();
  }
}

#ifdef GRANARY_WHERE_kernel

// Returns a new instruction that will "allocate" the spill slots by disabling
// interrupts.
void AllocateDisableInterrupts(InstructionList *GRANARY_IF_KERNEL(instrs)) {
  arch::Instruction ni;
  GRANARY_IF_KERNEL(APP(
      CALL_NEAR_RELBRd(&ni, DisableInterruptsFunction())));
}

// Returns a new instruction that will "allocate" the spill slots by enabling
// interrupts.
void AllocateEnableInterrupts(InstructionList *GRANARY_IF_KERNEL(instrs)) {
  arch::Instruction ni;
  GRANARY_IF_KERNEL(APP(
      CALL_NEAR_RELBRd(&ni, EnableInterruptsFunction())));
}

#endif  // GRANARY_WHERE_kernel

// Returns a new instruction that will allocate some stack space for virtual
// register slots.
NativeInstruction *AllocateStackSpace(int num_bytes) {
  arch::Instruction ni;
  arch::LEA_GPRv_AGEN(
      &ni, XED_REG_RSP,
      arch::BaseDispMemOp(num_bytes, XED_REG_RSP, arch::ADDRESS_WIDTH_BITS));
  return new NativeInstruction(&ni);
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
  auto mem_width = instr->instruction.effective_operand_width;
  GRANARY_ASSERT(0 < mem_width);
  auto mem_op(arch::BaseDispMemOp(adjusted_offset, XED_REG_RSP, mem_width));

  if (op.IsRegister()) {
    MOV_MEMv_GPRv(&(instr->instruction), mem_op, op.reg);
    instr->instruction.effective_operand_width = mem_width;

  } else if (op.IsImmediate()) {

    // Note: Templated `ImmediateBuilder` in `MOV_MEMv_IMMz` uses the type of
    //       `imm` as a hint about the true width of `imm`.
    if (16 == mem_width) {
      const auto imm = static_cast<uint16_t>(op.imm.as_uint);
      GRANARY_ASSERT(imm == op.imm.as_uint);
      MOV_MEMv_IMMz(&(instr->instruction), mem_op, imm);
    } else {
      const auto imm = static_cast<uint32_t>(op.imm.as_uint);
      GRANARY_ASSERT(imm == op.imm.as_uint);
      MOV_MEMv_IMMz(&(instr->instruction), mem_op, imm);
    }

    instr->instruction.effective_operand_width = mem_width;

  // Things like `PUSH_FS/GS`, and `PUSH_MEMv` should have already
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
    GRANARY_ASSERT(0 < mem_width);
    arch::MOV_GPRv_MEMv(
        &(instr->instruction),
        op.reg,
        arch::BaseDispMemOp(adjusted_offset, XED_REG_RSP, mem_width));
    instr->instruction.effective_operand_width = mem_width;

  // Things like `POP_FS/GS` and `POP_MEMv` should have already been
  // early mangled.
  } else {
    GRANARY_ASSERT(false);
  }
}

// Returns true if an architectural operand looks like a spill slot.
static bool IsSpillSlot(const arch::Operand &op) {
  return op.IsMemory() && !op.IsPointer() && !op.is_compound &&
         op.reg.IsVirtualSlot();
}

// Mangle the `PUSHF` and `PUSHFQ` instructions.
//
// Note: Early mangling adds a virtual register as the first (and only)
//       explicit operand of `instr->instruction`, precisely so that we can
//       safely make use of that virtual register here.
static void ManglePushFlags(Fragment *frag, NativeInstruction *instr,
                            int adjusted_offset) {
  arch::Instruction ni;
  auto &ainstr(instr->instruction);
  auto flag_access_reg = ainstr.ops[0].reg;
  auto op_width = instr->instruction.effective_operand_width;
  GRANARY_ASSERT(0 < op_width);
  arch::MOV_MEMv_GPRv(
      &ni,
      arch::BaseDispMemOp(adjusted_offset, XED_REG_RSP, op_width),
      flag_access_reg);
  ni.effective_operand_width = op_width;
  frag->instrs.InsertAfter(instr, new NativeInstruction(&ni));

  arch::POP_GPRv_51(&ni, flag_access_reg);
  ni.effective_operand_width = op_width;
  frag->instrs.InsertAfter(instr, new NativeInstruction(&ni));

  ainstr.ops[0].type = XED_ENCODER_OPERAND_TYPE_INVALID;
  ainstr.num_explicit_ops--;
}

// Mangle the `POPF` and `POPFQ` instructions.
//
// Note: Early mangling adds a virtual register as the first (and only)
//       explicit operand of `instr->instruction`, precisely so that we can
//       safely make use of that virtual register here.
static void ManglePopFlags(Fragment *frag, NativeInstruction *instr,
                           int adjusted_offset) {
  arch::Instruction ni;
  auto op_width = instr->instruction.effective_operand_width;
  GRANARY_ASSERT(0 < op_width);
  arch::PUSH_MEMv(&ni,
                  arch::BaseDispMemOp(adjusted_offset, XED_REG_RSP, op_width));
  ni.effective_operand_width = op_width;
  frag->instrs.InsertBefore(instr, new NativeInstruction(&ni));
}

// Adjust a memory operand if it refers to the stack pointer.
static void AdjustMemOp(arch::Operand *mem_op, int adjusted_offset) {
  if (!mem_op->IsExplicit()) return;
  if (mem_op->IsPointer()) return;

  if (mem_op->is_compound) {
    GRANARY_ASSERT(!mem_op->mem.index.IsStackPointer());
    if (mem_op->mem.base.IsStackPointer()) {
      mem_op->mem.disp += adjusted_offset;
    }
  } else if (mem_op->reg.IsStackPointer()) {
    *mem_op = arch::BaseDispMemOp(adjusted_offset, XED_REG_RSP,
                                  arch::GPR_WIDTH_BITS);
  }
}

static void GenericAdjustMemOps(arch::Instruction *instr, int adjusted_offset) {
  GRANARY_ASSERT(!instr->WritesToStackPointer());
  if (!instr->ReadsFromStackPointer()) return;
  for (auto &op : instr->ops) {
    if (op.IsMemory()) AdjustMemOp(&op, adjusted_offset);
  }
}

// Mangle a `MOV_GPRv_MEMv` or `MOV_MEMv_GPRv` instruction, where the `MEMv`
// operand might be an abstract spill slot or might be a stack pointer
// reference.
static void MangleMov(NativeInstruction *instr, int adjusted_offset) {
  auto &ainstr(instr->instruction);
  arch::Operand *mem_op(nullptr);
  if (IsSpillSlot(ainstr.ops[0])) {
    mem_op = &(ainstr.ops[0]);
  } else if (IsSpillSlot(ainstr.ops[1])) {
    mem_op = &(ainstr.ops[1]);
  }
  if (mem_op) {  // Found a spill slot.
    const auto new_mem_op = arch::BaseDispMemOp(
        static_cast<int32_t>(mem_op->reg.Number()) * arch::GPR_WIDTH_BYTES,
        XED_REG_RSP, arch::GPR_WIDTH_BITS);
    mem_op->mem = new_mem_op.mem;
    mem_op->is_compound = new_mem_op.is_compound;
    return;
  }

  if (ainstr.ops[0].IsMemory()) {
    mem_op = &(ainstr.ops[0]);
  } else if (ainstr.ops[1].IsMemory()) {
    mem_op = &(ainstr.ops[1]);
  }

  if (mem_op) {
    AdjustMemOp(mem_op, adjusted_offset);
  }
}

// Mangle a `XCHG_MEMv_GPRv`, where the `MEMv` operand might be an abstract
// spill slot or might be a stack pointer reference.
static void MangleXchg(NativeInstruction *instr, int adjusted_offset) {
  auto &ainstr(instr->instruction);
  auto mem_op = &(ainstr.ops[0]);
  if (!mem_op->IsMemory()) return;

  if (IsSpillSlot(ainstr.ops[0])) {
    const auto new_mem_op = arch::BaseDispMemOp(
        static_cast<int32_t>(mem_op->reg.Number()) * arch::GPR_WIDTH_BYTES,
        XED_REG_RSP, arch::GPR_WIDTH_BITS);
    mem_op->mem = new_mem_op.mem;
    mem_op->is_compound = new_mem_op.is_compound;
  } else  {
    AdjustMemOp(mem_op, adjusted_offset);
  }
}


// Mangle a `LEA` instruction.
static void MangleLEA(NativeInstruction *instr, int adjusted_offset) {
  auto &ainstr(instr->instruction);
  const auto &dst(ainstr.ops[0]);
  auto &src(ainstr.ops[1]);
  if (dst.reg.IsStackPointer()) {  // Stack pointer shift.
    if (src.is_compound) {
      GRANARY_ASSERT(src.mem.base.IsStackPointer() && !src.mem.index.IsValid());
    } else {  // No-op.
      GRANARY_ASSERT(src.reg.IsStackPointer());
    }
    GRANARY_ASSERT(!ainstr.is_sticky);
    NOP_90(&ainstr);
  } else {
    AdjustMemOp(&src, adjusted_offset);
  }
}

// Mangle simple arithmetic instructions that make constant changes to the
// stack pointer into `TEST` instructions based on the stack pointer, so as
// to approximately conserve the flags behavior.
static void MangleArith(NativeInstruction *instr, int adjusted_offset) {
  auto &ainstr(instr->instruction);
  if (!ainstr.ops[0].IsRegister() || !ainstr.ops[0].reg.IsStackPointer()) {
    GenericAdjustMemOps(&ainstr, adjusted_offset);
    return;
  }

  if (XED_ICLASS_ADD == ainstr.iclass || XED_ICLASS_SUB == ainstr.iclass) {
    GRANARY_ASSERT(ainstr.ops[1].IsImmediate());
  }

  // Note: This is not a perfect solution, but we don't really expect it to
  //       be all that bad either.
  //
  //       Things that this doesn't do a good job of preserving are the
  //       BCD adjust / auxiliary carry flag, and the parity flag.
  arch::TEST_GPRv_GPRv(&ainstr, XED_REG_RSP, XED_REG_RSP);
  ainstr.effective_operand_width = GPR_WIDTH_BITS;
}

}  // namespace

// Adjusts / mangles an instruction (potentially more than one) so that the
// usage of the stack pointer remains transparent, despite the fact that the
// native stack pointer has been changed to accommodate virtual register spills.
// Returns the next instruction on which we should operate.
//
// Note: This function has an architecture-specific implementation.
void AdjustStackInstruction(Fragment *frag, NativeInstruction *instr,
                            int adjusted_offset, int next_adjusted_offset) {
  auto &ainstr(instr->instruction);
  if (ainstr.is_stack_blind) return;

  switch (ainstr.iclass) {
    case XED_ICLASS_PUSH:
      ManglePush(instr, next_adjusted_offset);
      break;
    case XED_ICLASS_POP:
      ManglePop(instr, adjusted_offset);
      break;
    case XED_ICLASS_PUSHF:
    case XED_ICLASS_PUSHFQ:
      if (!instr->instruction.is_sticky) {
        ManglePushFlags(frag, instr, next_adjusted_offset);
      }
      break;
    case XED_ICLASS_POPF:
    case XED_ICLASS_POPFQ:
      ManglePopFlags(frag, instr, adjusted_offset);
      break;
    case XED_ICLASS_RET_NEAR:
      // TODO(pag): Handle specialized return!!!
      break;
    case XED_ICLASS_MOV:
      MangleMov(instr, adjusted_offset);
      break;
    case XED_ICLASS_XCHG:
      MangleXchg(instr, adjusted_offset);
      break;
    case XED_ICLASS_LEA:
      MangleLEA(instr, adjusted_offset);
      break;

    case XED_ICLASS_SUB:
    case XED_ICLASS_ADD:
    case XED_ICLASS_INC:
    case XED_ICLASS_DEC:
      MangleArith(instr, adjusted_offset);
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
      GenericAdjustMemOps(&ainstr, adjusted_offset);
      break;
  }
}
namespace {

static void AllocateSlot(Operand &op) {
  op = arch::SlotMemOp(os::SLOT_VIRTUAL_REGISTER, op.reg.Number(),
                       op.BitWidth());
}

// Replace any abstract spill slots in an instruction with concrete, segment-
// based spill slots.
static void AllocateSlots(NativeInstruction *instr) {
  if (!instr) return;
  auto &ainstr(instr->instruction);
  if (XED_ICLASS_MOV != ainstr.iclass && XED_ICLASS_XCHG != ainstr.iclass) {
    return;
  }
  if (IsSpillSlot(ainstr.ops[0])) AllocateSlot(ainstr.ops[0]);
  if (IsSpillSlot(ainstr.ops[1])) AllocateSlot(ainstr.ops[1]);
}

}  // namespace

// Allocates all remaining non-stack spill slots in some architecture and
// potentially mode (e.g. kernel/user) specific way.
void AllocateSlots(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (IsA<SSAFragment *>(frag)) {
      auto partition = frag->partition.Value();

      // Only do stack switching if the stack isn't valid. Same thing with
      // slot allocation
      if (partition->analyze_stack_frame) continue;

      for (auto instr : InstructionListIterator(frag->instrs)) {
        if (auto ainstr = DynamicCast<AnnotationInstruction *>(instr)) {
          if (kAnnotCondLeaveNativeStack == ainstr->annotation) {
            SwitchOffStack(&(frag->instrs), instr);
          } else if (kAnnotCondEnterNativeStack == ainstr->annotation) {
            SwitchOnStack(&(frag->instrs), instr);
          }
        } else if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
          AllocateSlots(ninstr);
        }
      }
    }
  }
}

}  // namespace arch
}  // namespace granary
