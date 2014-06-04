/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/x86-64/builder.h"

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"

namespace granary {

// Returns a new instruction that will allocate some stack space for virtual
// register slots.
NativeInstruction *AllocateStackSpace(int num_bytes) {
  arch::Instruction instr;
  arch::LEA_GPRv_AGEN(
      &instr, XED_REG_RSP,
      arch::BaseDispMemOp(num_bytes, XED_REG_RSP, arch::ADDRESS_WIDTH_BITS));
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
    GRANARY_ASSERT(0 < mem_width);
    arch::MOV_MEMv_GPRv(
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
    GRANARY_ASSERT(0 < mem_width);
    arch::MOV_GPRv_MEMv(
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
        mem_op->reg.Number() * 8, XED_REG_RSP, arch::GPR_WIDTH_BITS);
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
    if (mem_op->is_compound) {
      if (XED_REG_RSP == mem_op->mem.reg_base) {
        mem_op->mem.disp += adjusted_offset;
      }
    } else if (mem_op->reg.IsStackPointer()) {
      *mem_op = arch::BaseDispMemOp(adjusted_offset, XED_REG_RSP,
                                    arch::GPR_WIDTH_BITS);
    }
  }
}

// Mangle a `LEA` instruction.
static void MangleLEA(NativeInstruction *instr, int adjusted_offset) {
  auto &ainstr(instr->instruction);
  const auto &dst(ainstr.ops[0]);
  auto &src(ainstr.ops[1]);
  if (dst.reg.IsStackPointer()) {  // Stack pointer shift.
    if (src.is_compound) {
      GRANARY_ASSERT(XED_REG_RSP == src.mem.reg_base &&
                     XED_REG_INVALID == src.mem.reg_index);
    } else {  // Nop.
      GRANARY_ASSERT(src.reg.IsStackPointer());
    }
    NOP_90(&ainstr);
  } else if (src.is_compound) {
    if (XED_REG_RSP == src.mem.reg_base) {  // Read of an offset stack pointer.
      GRANARY_ASSERT(XED_REG_INVALID == src.mem.reg_index);
      src.mem.disp += adjusted_offset;
    }
  } else if (src.reg.IsStackPointer()) {  // Copy of the stack pointer.
    src = arch::BaseDispMemOp(
        adjusted_offset, XED_REG_RSP, arch::ADDRESS_WIDTH_BITS);
  }
}

// Mangle simple arithmetic instructions that make constant changes to the
// stack pointer into `TEST` instructions based on the stack pointer, so as
// to approximately conserve the flags behavior.
static void MangleArith(NativeInstruction *instr) {
  auto &ainstr(instr->instruction);
  if (!ainstr.ops[0].IsRegister()) return;
  if (!ainstr.ops[0].reg.IsStackPointer()) return;
  if (XED_ICLASS_ADD == ainstr.iclass || XED_ICLASS_SUB == ainstr.iclass) {
    GRANARY_ASSERT(ainstr.ops[1].IsImmediate());
  }

  // Note: This is not a perfect solution, but we don't really expect it to
  //       be all that bad either.
  //
  //       Things that this doesn't do a good job of preserving are the
  //       BCD adjust / auxiliary carry flag, and the parity flag.
  arch::TEST_GPRv_GPRv(&ainstr, XED_REG_RSP, XED_REG_RSP);
}

// Mangle an indirect call into a NOP, as it will fall-through to edge code.
static void MangleIndirectCFI(NativeInstruction *instr) {
  arch::NOP_90(&(instr->instruction));
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
  switch (ainstr.iclass) {
    case XED_ICLASS_PUSH:
      ManglePush(instr, next_adjusted_offset);
      break;
    case XED_ICLASS_POP:
      ManglePop(instr, adjusted_offset);
      break;
    case XED_ICLASS_PUSHF:
    case XED_ICLASS_PUSHFQ:
      ManglePushFlags(frag, instr, next_adjusted_offset);
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
    case XED_ICLASS_LEA:
      MangleLEA(instr, adjusted_offset);
      break;

    case XED_ICLASS_SUB:
    case XED_ICLASS_ADD:
    case XED_ICLASS_INC:
    case XED_ICLASS_DEC:
      MangleArith(instr);
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
      GRANARY_ASSERT(!ainstr.ReadsFromStackPointer() &&
                     !ainstr.WritesToStackPointer());
      break;
  }
}

// Mangle all indirect calls and jumps into NOPs.
void RemoveIndirectCallsAndJumps(Fragment *frag) {
  for (auto instr : InstructionListIterator(frag->instrs)) {
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if (!(ninstr->IsUnconditionalJump() || ninstr->IsFunctionCall())) {
        continue;
      }
      if (ninstr->HasIndirectTarget()) {
        MangleIndirectCFI(ninstr);  // Convert to a NO-OP.
      }
    }
  }

}

#if defined(GRANARY_WHERE_kernel) || !defined(GRANARY_WHERE_user)
# error "TODO(pag): Implement `ArchAllocateSlots` for kernel space."
#else

namespace {
extern "C" {

// Get the base address of the current thread's TLS. We use this address to
// compute `FS`-based offsets from the TLS base. We assume that the base address
// returned by this function is the address associated with `FS:0`.
extern intptr_t granary_arch_get_tls_base(void);

}  // extern C

// Per-thread spill slots.
//
// Note: This depends on a load-time TLS implementation, as is the case on
//       systems like Linux.
static __thread __attribute__((tls_model("initial-exec"))) struct {
  intptr_t slots[SpillInfo::MAX_NUM_SPILL_SLOTS];
} SPILL_SLOTS;

static void ArchAllocateSlot(arch::Operand &op) {
  auto slot = op.reg.Number();
  auto this_slot = &(SPILL_SLOTS.slots[slot]);
  auto this_slot_addr = reinterpret_cast<intptr_t>(this_slot);
  auto slot_offset = this_slot_addr - granary_arch_get_tls_base();
  op.segment = XED_REG_FS;  // Linux-specific.
  op.is_compound = true;
  op.mem.disp = static_cast<int32_t>(slot_offset);
  op.mem.reg_base = XED_REG_INVALID;
  op.mem.reg_index = XED_REG_INVALID;
  op.mem.scale = 0;
}

// Replace any abstract spill slots in an instruction with concrete, segment-
// based spill slots.
static void ArchAllocateSlots(NativeInstruction *instr) {
  if (!instr) return;
  auto &ainstr(instr->instruction);
  if (XED_ICLASS_MOV != ainstr.iclass) return;

  if (ainstr.ops[0].IsMemory()) {
    if (ainstr.ops[0].reg.IsVirtualSlot()) ArchAllocateSlot(ainstr.ops[0]);
  } else if (ainstr.ops[1].IsMemory()) {
    if (ainstr.ops[1].reg.IsVirtualSlot()) ArchAllocateSlot(ainstr.ops[1]);
  }
}

}  // namespace

// Allocates all remaining non-stack spill slots in some architecture and
// potentially mode (e.g. kernel/user) specific way.
void ArchAllocateSlots(FragmentList *frags) {
  for (auto frag : FragmentListIterator(frags)) {
    if (IsA<SSAFragment *>(frag)) {
      for (auto instr : InstructionListIterator(frag->instrs)) {
        if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
          ArchAllocateSlots(ninstr);
        }
      }
    }
  }
}

#endif  // User-space implementation of slots.

}  // namespace granary
