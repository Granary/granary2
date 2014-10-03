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

#define BEFORE_NOSTACK(f, i, ...) \
  do { \
    __VA_ARGS__ ; \
    ni.is_stack_blind = true; \
    f->instrs.InsertBefore(i, new NativeInstruction(&ni)); \
  } while (0)

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
                        Instruction &ni) {
  auto &ainstr(instr->instruction);
  switch (ainstr.iform) {
    case XED_IFORM_MOV_SEG_MEMw:
      BEFORE_NOSTACK(frag, instr, PUSH_MEMv(&ni, ainstr.ops[1]);
                     ni.effective_operand_width = 64);
      return 1;
    case XED_IFORM_MOV_SEG_GPR16:
      BEFORE_NOSTACK(frag, instr, PUSH_GPRv_50(&ni,
                                               ainstr.ops[1].reg.WidenedTo(8));
                     ni.effective_operand_width = 64);
      return 1;
    case XED_IFORM_MOV_MEMb_IMMb:
      BEFORE_NOSTACK(frag, instr, PUSH_GPRv_50(&ni, ainstr.ops[0].reg);
                     ni.effective_operand_width = 64);
      BEFORE_NOSTACK(frag, instr, PUSH_IMMb(&ni, ainstr.ops[1]);
                     ni.effective_operand_width = 64);
      return 2;

    default: return 0;
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
  auto num_pushed_ops = PushOperands(frag, instr, ni);
  auto undo_push_op = BaseDispMemOp(num_pushed_ops * 8,
                                    XED_REG_RSP, ADDRESS_WIDTH_BITS);

  // Put these just before `ainstr` so that if any VR rescheduling was done,
  // then it will be undone by the time the bottom-up pass hits `ainstr`, and
  // so the registers used by `ainstr` will all be "right".
  BEFORE_NOSTACK(frag, instr, CALL_NEAR(&ni, EstimatedCachePC(),
                                        instr->emulation_pc,
                                        &(meta->native_addresses)));

  // `instr` is here. We leave it in place so that the virtual register system
  // can ensure the all native regs in use in it will be correct here.
  ainstr.dont_encode = true;

  // This is tricky: What happens is that the CALL will either return to the
  // same place (no fault), or it will add 5 bytes to its return address, and
  // thus return to the `recovery_branch` instruction below. The
  // `recovery_branch` instruction will redirect execution down the fault
  // recovery path.
  auto no_fault_label = new LabelInstruction;
  auto fault_label = new LabelInstruction;
  JMP_RELBRd(&ni, no_fault_label);
  frag->instrs.Append(new BranchInstruction(&ni, no_fault_label));

  if (num_pushed_ops) {
    // Restore RSP if there was a fault.
    APP_NOSTACK(frag, LEA_GPRv_AGEN(&ni, XED_REG_RSP, undo_push_op));
  }

  JMP_RELBRd(&ni, fault_label);
  auto recovery_branch = new BranchInstruction(&ni, fault_label);
  frag->instrs.Append(recovery_branch);
  frag->branch_instr = recovery_branch;
  frag->instrs.Append(no_fault_label);
  if (num_pushed_ops) {
    // Restore RSP if there wasn't a fault.
    APP_NOSTACK(frag, LEA_GPRv_AGEN(&ni, XED_REG_RSP, undo_push_op));
  }

  auto recovery_frag = MakeCodeSuccessor(frags, frag, FRAG_SUCC_BRANCH);
  recovery_frag->attr.can_add_succ_to_partition = false;
  recovery_frag->instrs.Append(fault_label);

  for (auto gpr : instr->used_regs) {
    auto saved_gpr = instr->saved_regs[gpr.Number()];
    RegisterOperand gpr_op(gpr);

    // If the operand is a read/write operand, then assume that it's a string
    // operation (e.g. MOVSB or REP MOVSB) and so then we should not restore
    // the register to exactly its old state, but to a close-enough version of
    // it.
    //
    // TODO(pag): It might actually be correct (for some OSes) to restore the
    //            regs to their original states, and assume that the recovery
    //            code is sufficiently general to handle re-doing some work.
    if (instr->MatchOperands(ExactReadAndWriteTo(gpr_op))) {
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
