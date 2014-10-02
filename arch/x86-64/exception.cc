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

static CodeFragment *MakeCodeSuccessor(FragmentList *frags, CodeFragment *frag,
                                       FragmentSuccessorSelector sel) {
  auto succ = new CodeFragment;
  succ->attr.block_meta = frag->attr.block_meta;
  succ->attr.reads_flags = true;
  succ->attr.modifies_flags = true;
  succ->attr.has_native_instrs = true;
  succ->attr.num_predecessors = 1;
  succ->stack = frag->stack;
  succ->type = CODE_TYPE_INST;
  succ->partition.Union(succ, frag);
  frag->successors[sel] = succ;
  frags->InsertAfter(frag, succ);
  return succ;
}

}  // namespace

// Process an exceptional control-flow instruction.
//
// Note: `instr` already belongs to `frag`.
void ProcessExceptionalCFI(FragmentList *frags, CodeFragment *frag,
                           ExceptionalControlFlowInstruction *instr) {
  Instruction ni;
  auto &ainstr(instr->instruction);
  auto meta = MetaDataCast<CacheMetaData *>(frag->attr.block_meta);

  // `instr` is here. We leave it in place so that the virtual register system
  // can make decisions based on it.
  ainstr.dont_encode = true;

  APP_NOSTACK(frag, PUSH_IMMb(&ni, static_cast<uint8_t>(0)));
  APP_NOSTACK(frag, CALL_NEAR(&ni, EstimatedCachePC(), instr->emulation_pc,
                              &(meta->native_addresses)));

  auto check_frag = MakeCodeSuccessor(frags, frag, FRAG_SUCC_FALL_THROUGH);
  auto test_op = BaseDispMemOp(8, XED_REG_RSP, arch::GPR_WIDTH_BITS);
  auto fault_label = new LabelInstruction;
  APP_NOSTACK(check_frag, TEST_MEMv_GPRv(&ni, test_op, XED_REG_RSP));
  APP_NOSTACK(check_frag, LEA_GPRv_AGEN(&ni, XED_REG_RSP, test_op));

  JNZ_RELBRd(&ni, fault_label);
  check_frag->instrs.Append(new BranchInstruction(&ni, fault_label));
  check_frag->attr.branch_is_jump = true;

  auto recovery_frag = MakeCodeSuccessor(frags, frag, FRAG_SUCC_BRANCH);
  const auto rcx = VirtualRegister::FromNative(XED_REG_RCX);
  for (auto gpr : instr->used_regs) {
    auto saved_gpr = instr->saved_regs[gpr.Number()];

    // Restore only high 32-bits of the GPR. We'll assume this is a REP
    // instruction.
    if ((ainstr.has_prefix_rep || ainstr.has_prefix_repne) && rcx != gpr) {
      APP(recovery_frag, BSWAP_GPRv(&ni, gpr));
      APP(recovery_frag, BSWAP_GPRv(&ni, saved_gpr));
      APP(recovery_frag, MOV_GPRv_GPRv_89(&ni, gpr.WidenedTo(32),
                                          saved_gpr.WidenedTo(32)));
      APP(recovery_frag, BSWAP_GPRv(&ni, gpr));

    // Restore the GPR itself.
    } else {
      APP(recovery_frag, MOV_GPRv_GPRv_89(&ni, gpr, saved_gpr));
    }
  }

  // TODO(pag): Still need to hook everything in with the successors. Also
  //            somehow need to get a fall-through successor properly
  //            hooked up with the instruction following `instr`. At the
  //            moment, I probably did `ProcessExceptionCFI` in the frag list
  //            builder wrong because it doesn't recall the instruction
  //            immediately following `instr`.
}

}  // namespace arch
}  // namespace granary
