/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/util.h"

#include "arch/x86-64/base.h"
#include "arch/x86-64/builder.h"
#include "arch/x86-64/context.h"
#include "arch/x86-64/slot.h"

#include "granary/code/fragment.h"

#include "granary/breakpoint.h"
#include "granary/cache.h"
#include "granary/context.h"

#define ENC(...) \
  do { \
    __VA_ARGS__ ; \
    GRANARY_IF_DEBUG( auto ret = ) stage_enc.Encode(&ni, pc); \
    GRANARY_ASSERT(ret); \
    GRANARY_IF_DEBUG( ret = ) commit_enc.EncodeNext(&ni, &pc); \
    GRANARY_ASSERT(ret); \
  } while (0)

#define APP(...) \
  do { \
    __VA_ARGS__ ; \
    ni.is_stack_blind = true; \
    call_frag->instrs.Append(new NativeInstruction(&ni)); \
  } while (0)

#define APP_INSTR(i) call_frag->instrs.Append(i)

namespace granary {
namespace arch {
namespace {

// Generates the wrapper code for a context callback.
void GenerateContextCallCode(Callback *callback) {
  Instruction ni;
  InstructionEncoder stage_enc(InstructionEncodeKind::STAGED);
  InstructionEncoder commit_enc(InstructionEncodeKind::COMMIT);
  auto pc = callback->wrapped_callback;

  // Save the flags.
  ENC(PUSHFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

  // Disable interrupts and swap stacks.
  if (GRANARY_IF_USER_ELSE(false, true)) {
    ENC(CLI(&ni));
    ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(os::SLOT_PRIVATE_STACK), XED_REG_RSP));
  }

  // Save the GPRs.
  ENC(PUSH_GPRv_50(&ni, XED_REG_RAX); );
  ENC(PUSH_GPRv_50(&ni, XED_REG_RCX); );
  ENC(PUSH_GPRv_50(&ni, XED_REG_RDX); );
  ENC(PUSH_GPRv_50(&ni, XED_REG_RBX); );
  ENC(PUSH_GPRv_50(&ni, XED_REG_RBP); );
  ENC(PUSH_GPRv_50(&ni, XED_REG_RSI); );
  ENC(PUSH_GPRv_50(&ni, XED_REG_RDI); );
  ENC(PUSH_GPRv_50(&ni, XED_REG_R8); );
  ENC(PUSH_GPRv_50(&ni, XED_REG_R9); );
  ENC(PUSH_GPRv_50(&ni, XED_REG_R10); );
  ENC(PUSH_GPRv_50(&ni, XED_REG_R11); );
  ENC(PUSH_GPRv_50(&ni, XED_REG_R12); );
  ENC(PUSH_GPRv_50(&ni, XED_REG_R13); );
  ENC(PUSH_GPRv_50(&ni, XED_REG_R14); );
  ENC(PUSH_GPRv_50(&ni, XED_REG_R15); );

  // Treat the pushed GPRs as `MachineContext`, and pass a pointer to them as
  // arg1.
  //
  // TODO(pag): Remove ABI-specific use of RDI and RSI.
  ENC(MOV_GPRv_GPRv_89(&ni, XED_REG_RDI, XED_REG_RSP));

  // TODO(pag): Alignment?

  // Call the callback.
  ENC(CALL_NEAR(&ni, pc, callback->callback, &(callback->callback)));

  // Restore the GPRs.
  ENC(POP_GPRv_51(&ni, XED_REG_R15); );
  ENC(POP_GPRv_51(&ni, XED_REG_R14); );
  ENC(POP_GPRv_51(&ni, XED_REG_R13); );
  ENC(POP_GPRv_51(&ni, XED_REG_R12); );
  ENC(POP_GPRv_51(&ni, XED_REG_R11); );
  ENC(POP_GPRv_51(&ni, XED_REG_R10); );
  ENC(POP_GPRv_51(&ni, XED_REG_R9); );
  ENC(POP_GPRv_51(&ni, XED_REG_R8); );
  ENC(POP_GPRv_51(&ni, XED_REG_RDI); );
  ENC(POP_GPRv_51(&ni, XED_REG_RSI); );
  ENC(POP_GPRv_51(&ni, XED_REG_RBP); );
  ENC(POP_GPRv_51(&ni, XED_REG_RBX); );
  ENC(POP_GPRv_51(&ni, XED_REG_RDX); );
  ENC(POP_GPRv_51(&ni, XED_REG_RCX); );
  ENC(POP_GPRv_51(&ni, XED_REG_RAX); );

  // Swap back to the application stack.
  if (GRANARY_IF_USER_ELSE(false, true)) {
    ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(os::SLOT_PRIVATE_STACK), XED_REG_RSP));
  }

  // Restore the flags (and potentially interrupts).
  ENC(POPFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

  ENC(RET_NEAR(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

  GRANARY_ASSERT(CONTEXT_CALL_CODE_SIZE_BYTES >=
                 (pc - callback->wrapped_callback));
}

}  // namespace

// Generates the wrapper code for a context callback.
Callback *GenerateContextCallback(AppPC func_pc) {
  auto edge_code = AllocateCode(kCodeCacheKindCold,
                                CONTEXT_CALL_CODE_SIZE_BYTES);
  auto callback = new Callback(func_pc, edge_code);
  CodeCacheTransaction transaction;
  GenerateContextCallCode(callback);
  return callback;
}

// Generates some code to target some client function. The generated code saves
// the machine context and passes it directly to the client function for direct
// manipulation.
CodeFragment *CreateContextCallFragment(Context *context,
                                        FragmentList *frags, CodeFragment *pred,
                                        AppPC func_pc) {
  Instruction ni;
  UsedRegisterSet all_regs;

  auto call_frag = new CodeFragment;
  auto cc = context->ContextCallback(func_pc);
  GRANARY_ASSERT(nullptr != cc->wrapped_callback);

  pred->successors[kFragSuccFallThrough] = call_frag;
  frags->InsertAfter(pred, call_frag);
  all_regs.ReviveAll();

  APP_INSTR(new AnnotationInstruction(kAnnotCondLeaveNativeStack));
  APP_INSTR(new AnnotationInstruction(kAnnotReviveRegisters, all_regs));
  APP(CALL_NEAR_RELBRd(&ni, cc->wrapped_callback);
      ni.is_stack_blind = true);
  APP_INSTR(new AnnotationInstruction(kAnnotCondEnterNativeStack));

  return call_frag;
}

}  // namespace arch
}  // namespace granary
