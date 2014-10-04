/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"

#include "granary/cache.h"
#include "granary/context.h"
#include "granary/metadata.h"

// After `cache.h` to get `NativeAddress`.
#include "arch/x86-64/builder.h"

#define APP_NOSTACK(f, ...) \
  do { \
    __VA_ARGS__ ; \
    ni.is_stack_blind = true; \
    f->instrs.Append(new NativeInstruction(&ni)); \
  } while (0)

#define APP(f, ...) \
  do { \
    __VA_ARGS__ ; \
    f->instrs.Append(new NativeInstruction(&ni)); \
  } while (0)

namespace granary {
namespace arch {

// Save some architectural state before `instr` executes, so that if a
// recoverable exception occurs while executing `instr`, we can handle it.
void SaveStateForExceptionCFI(DecodedBasicBlock *block,
                              ExceptionalControlFlowInstruction *instr,
                              granary::Instruction *before_instr) {
  Instruction ni;
  for (auto gpr : instr->used_regs) {
    auto vr = block->AllocateVirtualRegister();
    instr->saved_regs[gpr.Number()] = vr;
    MOV_GPRv_GPRv_89(&ni, vr, gpr);
    before_instr->InsertAfter(new NativeInstruction(&ni));
  }
}

namespace {

// Make a successor for a fragment where an instruction in the fragment
// might trigger an exception.
static CodeFragment *MakeCodeSuccessor(FragmentList *frags, CodeFragment *frag,
                                       FragmentSuccessorSelector sel) {
  auto succ = new CodeFragment;
  succ->attr.block_meta = frag->attr.block_meta;
  succ->attr.has_native_instrs = true;
  succ->attr.num_predecessors = 1;
  succ->stack = frag->stack;
  succ->type = CODE_TYPE_INST;
  succ->partition.Union(succ, frag);
  frag->successors[sel] = succ;
  frags->InsertAfter(frag, succ);
  return succ;
}

// Pushes any explicitly needed operands onto the stack. Returns the number of
// pushed operands.
static int PushOperands(CodeFragment *frag,
                        ExceptionalControlFlowInstruction *instr,
                        Instruction &ni, VirtualRegister *pop_on_sucess) {
  auto &ainstr(instr->instruction);
  switch (ainstr.iform) {
    case XED_IFORM_MOV_SEG_MEMw:
      APP_NOSTACK(frag, PUSH_MEMv(&ni, ainstr.ops[1]);
                  ni.effective_operand_width = 64);
      return 1;

    case XED_IFORM_MOV_SEG_GPR16:
      APP_NOSTACK(frag, PUSH_GPRv_50(&ni, ainstr.ops[1].reg.WidenedTo(8));
                  ni.effective_operand_width = 64);
      return 1;

    case XED_IFORM_MOV_MEMb_IMMb:
      APP_NOSTACK(frag, PUSH_GPRv_50(&ni, ainstr.ops[0].reg);
                  ni.effective_operand_width = 64);
      APP_NOSTACK(frag, PUSH_IMMb(&ni, ainstr.ops[1]);
                  ni.effective_operand_width = 64);
      return 2;

    case XED_IFORM_MOV_MEMb_GPR8:
    case XED_IFORM_MOV_MEMv_GPRv:
      APP_NOSTACK(frag, PUSH_GPRv_50(&ni, ainstr.ops[0].reg);
                  ni.effective_operand_width = 64);
      APP_NOSTACK(frag, PUSH_GPRv_50(&ni, ainstr.ops[1].reg.WidenedTo(8));
                  ni.effective_operand_width = 64);
      return 2;

    // Handle `XCHG`. One trickiness is that we need to pop the source operand
    // to make sure that the code sees the change.
    case XED_IFORM_XCHG_MEMb_GPR8:
    case XED_IFORM_XCHG_MEMv_GPRv: {
      APP_NOSTACK(frag, PUSH_GPRv_50(&ni, ainstr.ops[0].reg);
                  ni.effective_operand_width = 64);
      auto push_reg = ainstr.ops[1].reg.WidenedTo(8);
      APP_NOSTACK(frag, PUSH_GPRv_50(&ni, push_reg);
                  ni.effective_operand_width = 64);
      pop_on_sucess[1] = push_reg;
      return 2;
    }

    case XED_IFORM_MOV_GPR8_MEMb:
    case XED_IFORM_MOV_GPRv_MEMv: {
      auto push_reg = ainstr.ops[0].reg.WidenedTo(8);
      APP_NOSTACK(frag, PUSH_GPRv_50(&ni, push_reg);
                  ni.effective_operand_width = 64);
      pop_on_sucess[0] = push_reg;
      APP_NOSTACK(frag, PUSH_GPRv_50(&ni, ainstr.ops[1].reg);
                  ni.effective_operand_width = 64);
      return 2;
    }

    // Handle `FXRSTOR64` and `PREFETCHT0`.
    case XED_IFORM_FXRSTOR64_MEMmfpxenv:
    case XED_IFORM_PREFETCHT0_MEMmprefetch:
      APP_NOSTACK(frag, PUSH_GPRv_50(&ni, ainstr.ops[0].reg);
                  ni.effective_operand_width = 64);
      return 1;

    default: return 0;
  }
}

// Find which registers are read and written in the instruction.
static void FindRWRegs(ExceptionalControlFlowInstruction *instr, bool *is_rw) {
  for (auto gpr : instr->used_regs) {
    RegisterOperand gpr_op(gpr);
    is_rw[gpr.Number()] = instr->MatchOperands(ExactReadAndWriteTo(gpr_op));
  }
}

// Restore the registers to their saved states (or a modification thereof) in
// the event that an exception occurred.
static void RestoreRegsOnFailure(CodeFragment *recovery_frag,
                                 ExceptionalControlFlowInstruction *instr,
                                 Instruction &ni, bool *is_rw) {
  const auto &ainstr(instr->instruction);
  const auto is_restartable = ainstr.has_prefix_rep || ainstr.has_prefix_repne;
  for (auto gpr : instr->used_regs) {
    const auto gpr_num = gpr.Number();
    auto saved_gpr = instr->saved_regs[gpr_num];

    // Restore the register to a close-enough version of it's old state that
    // also hopefully maintains the changes that successfully completed before
    // the instruction was interrupted.
    if (is_restartable && is_rw[gpr_num]) {
      APP(recovery_frag, BSWAP_GPRv(&ni, gpr));
      APP(recovery_frag, BSWAP_GPRv(&ni, saved_gpr));
      APP(recovery_frag, MOV_GPRv_GPRv_89(&ni, gpr.WidenedTo(4),
                                          saved_gpr.WidenedTo(4)));
      APP(recovery_frag, BSWAP_GPRv(&ni, gpr));

    // Restore the GPR itself. There might be some redundancy here for read-
    // only operands, but whatever.
    } else {
      APP(recovery_frag, MOV_GPRv_GPRv_89(&ni, gpr, saved_gpr));
    }
  }
}

// Corrects the stack pointer after an exception to account for any operand
// spilling performed before the emulated instruction.
static void UnspillRegsOnFailure(CodeFragment *frag,  Instruction &ni,
                                 int num_pushed_ops) {
  if (!num_pushed_ops) return;
  // Restore RSP if there was a fault.
  APP_NOSTACK(frag, LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                                  BaseDispMemOp(num_pushed_ops * 8,
                                                XED_REG_RSP,
                                                ADDRESS_WIDTH_BITS)));
}

// Restore the stack pointer back to where it was supposed to be. This also
// allows us to communicate register state changes from the emulated
// instruction back into the application by popping off specific registers.
static void UnspillRegsOnSuccess(CodeFragment *frag, Instruction &ni,
                                 VirtualRegister *pop_on_sucess,
                                 int num_pushed_ops) {
  for (auto i = num_pushed_ops; i-- > 0; ) {
    if (pop_on_sucess[i].IsValid()) {
      APP_NOSTACK(frag, POP_GPRv_51(&ni, pop_on_sucess[i]);
                        ni.effective_operand_width = GPR_WIDTH_BITS;);
    } else {
      APP_NOSTACK(frag, LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                                      BaseDispMemOp(8, XED_REG_RSP,
                                                    ADDRESS_WIDTH_BITS)));
    }
  }
}

}  // namespace

// Process an exceptional control-flow instruction. Returns the predecessor of
// the fall-through fragment.
//
// Note: `instr` already belongs to `frag`.
CodeFragment *ProcessExceptionalCFI(FragmentList *frags, CodeFragment *frag,
                                    ExceptionalControlFlowInstruction *instr) {
  Instruction ni;
  auto &ainstr(instr->instruction);
  auto meta = MetaDataCast<CacheMetaData *>(frag->attr.block_meta);
  VirtualRegister pop_on_sucess[] = {VirtualRegister(), VirtualRegister()};
  auto num_pushed_ops = PushOperands(frag, instr, ni, pop_on_sucess);

  // Detect if a register is read and written in the instruction.
  bool is_rw[NUM_GENERAL_PURPOSE_REGISTERS] = {false};
  FindRWRegs(instr, is_rw);

  // Emulate the original instruction.
  APP_NOSTACK(frag, CALL_NEAR(&ni, EstimatedCachePC(), instr->emulation_pc,
                              &(meta->native_addresses)));

  // This is tricky: What happens is that the CALL will either return to the
  // same place (no fault), or it will add 5 bytes to its return address, and
  // thus return to the `recovery_branch` instruction below. The
  // `recovery_branch` instruction will redirect execution down the fault
  // recovery path.
  auto no_fault_label = new LabelInstruction;
  auto fault_label = new LabelInstruction;
  JMP_RELBRd(&ni, no_fault_label);
  frag->instrs.Append(new BranchInstruction(&ni, no_fault_label));

  UnspillRegsOnFailure(frag, ni, num_pushed_ops);

  JMP_RELBRd(&ni, fault_label);
  auto recovery_branch = new BranchInstruction(&ni, fault_label);
  frag->instrs.Append(recovery_branch);
  frag->branch_instr = recovery_branch;
  frag->instrs.Append(no_fault_label);

  UnspillRegsOnSuccess(frag, ni, pop_on_sucess, num_pushed_ops);

  // `instr` is here. We leave it in place so that the virtual register system
  // can ensure the all native regs in use in it will be correct here.
  ainstr.dont_encode = true;
  frag->instrs.Remove(instr);
  frag->instrs.Append(instr);

  // End the fragment with `instr`. This ideally ensures that all regs that
  // need to be scheduled will be in there correct places before any of the
  // above mess.
  auto recovery_frag = MakeCodeSuccessor(frags, frag, FRAG_SUCC_BRANCH);
  recovery_frag->attr.can_add_succ_to_partition = false;
  recovery_frag->instrs.Append(fault_label);

  RestoreRegsOnFailure(recovery_frag, instr, ni, is_rw);

  // The fragment builder will have associated an exit fragment with the
  // exception handling fragment. We'll add it as a successor of the recovery
  // path.
  auto except_frag = instr->TargetBlock()->fragment;
  GRANARY_ASSERT(nullptr != except_frag);
  recovery_frag->successors[FRAG_SUCC_FALL_THROUGH] = except_frag;

  return frag;
}

}  // namespace arch
}  // namespace granary
