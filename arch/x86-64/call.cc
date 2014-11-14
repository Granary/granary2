/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/driver.h"
#include "arch/util.h"
#include "arch/x86-64/builder.h"
#include "arch/x86-64/slot.h"

#include "granary/cfg/lir.h"

#include "granary/code/fragment.h"
#include "granary/code/inline_assembly.h"

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
    frag->instrs.Append(new NativeInstruction(&ni)); \
  } while (0)

#define APP_INSTR(i) frag->instrs.Append(i)

namespace granary {
namespace arch {
namespace {

enum : bool {
#ifdef GRANARY_OS_linux
  USING_LINUX_ITANIUM_ABI = true
#else
  static_assert(false, "TODO(pag): Implement me.");
  USING_LINUX_ITANIUM_ABI = false
#endif
};

// Generates the wrapper code for a context callback.
void GenerateInlineCallCode(Callback *callback, int num_args) {
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
  if (6 > num_args) ENC(PUSH_GPRv_50(&ni, XED_REG_R10); );
  if (5 > num_args) ENC(PUSH_GPRv_50(&ni, XED_REG_R11); );
  if (4 > num_args) ENC(PUSH_GPRv_50(&ni, XED_REG_R12); );
  if (3 > num_args) ENC(PUSH_GPRv_50(&ni, XED_REG_R13); );
  if (2 > num_args) ENC(PUSH_GPRv_50(&ni, XED_REG_R14); );
  if (1 > num_args) ENC(PUSH_GPRv_50(&ni, XED_REG_R15); );

  if (1 <= num_args) ENC(MOV_GPRv_GPRv_89(&ni, XED_REG_RDI, XED_REG_R15));
  if (2 <= num_args) ENC(MOV_GPRv_GPRv_89(&ni, XED_REG_RSI, XED_REG_R14));
  if (3 <= num_args) ENC(MOV_GPRv_GPRv_89(&ni, XED_REG_RDX, XED_REG_R13));
  if (4 <= num_args) ENC(MOV_GPRv_GPRv_89(&ni, XED_REG_RCX, XED_REG_R12));
  if (5 <= num_args) ENC(MOV_GPRv_GPRv_89(&ni, XED_REG_R8, XED_REG_R11));
  if (6 <= num_args) ENC(MOV_GPRv_GPRv_89(&ni, XED_REG_R9, XED_REG_R10));

  // Call the callback.
  ENC(CALL_NEAR(&ni, pc, callback->callback, &(callback->callback)));

  // Restore the GPRs.
  if (1 > num_args) ENC(POP_GPRv_51(&ni, XED_REG_R15); );
  if (2 > num_args) ENC(POP_GPRv_51(&ni, XED_REG_R14); );
  if (3 > num_args) ENC(POP_GPRv_51(&ni, XED_REG_R13); );
  if (4 > num_args) ENC(POP_GPRv_51(&ni, XED_REG_R12); );
  if (5 > num_args) ENC(POP_GPRv_51(&ni, XED_REG_R11); );
  if (6 > num_args) ENC(POP_GPRv_51(&ni, XED_REG_R10); );
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

  GRANARY_ASSERT(INLINE_CALL_CODE_SIZE_BYTES >=
                 (pc - callback->wrapped_callback));
}

// Copies an operand that should be passed into the client function into a
// temporary holding place (the virtual register `reg`). We first copy into
// a temporary holding place so that if the operands reference a register that
// is also an argument register (RDI, RSI, etc), then we'll see the right value
// and not one overwritten by a different argument setup instruction.
static void CopyOperand(CodeFragment *frag, Instruction &ni,
                        VirtualRegister reg, const granary::Operand &op) {
  const auto &aop(*op.Extract());
  if (aop.IsMemory()) {
    reg.Widen(op.ByteWidth());
    APP(MOV_GPRv_MEMv(&ni, reg, aop);
        ni.ops[0].is_definition = true; );

  } else if (aop.IsImmediate()) {
    // In practice, we want to use the widest possible GPR to help with later
    // copy propagation.
    if (32 >= aop.BitWidth()) {
      APP(MOV_GPRv_IMMv(&ni, reg, static_cast<uint32_t>(aop.imm.as_uint));
          ni.ops[0].is_definition = true; );
    } else {
      APP(MOV_GPRv_IMMv(&ni, reg, aop);
          ni.ops[0].is_definition = true; );
    }

  } else if (aop.IsRegister()) {
    auto src_reg = aop.reg;
    if (src_reg.IsGeneralPurpose()) {
      src_reg.Widen(GPR_WIDTH_BYTES);
    } else {
      // TODO(pag): Handle non-GPRs that need special instructions, e.g. MOV_CR.
      reg.Widen(op.ByteWidth());
    }
    APP(MOV_GPRv_GPRv_89(&ni, reg, src_reg);
        ni.ops[0].is_definition = true; );
  } else {
    GRANARY_ASSERT(false);
  }
}

}  // namespace

// Generates the wrapper code for an outline callback.
Callback *GenerateInlineCallback(CodeCache *cache, InlineFunctionCall *call) {
  auto edge_code = cache->AllocateBlock(INLINE_CALL_CODE_SIZE_BYTES);
  auto callback = new Callback(call->target_app_pc, edge_code);
  CodeCacheTransaction transaction(
      cache, edge_code, edge_code + INLINE_CALL_CODE_SIZE_BYTES);
  GenerateInlineCallCode(callback, call->NumArguments());
  return callback;
}

#define SAVE_ARG(arg, reg) \
  if (arg < num_args) \
    APP_INSTR(new AnnotationInstruction(IA_SSA_SAVE_REG, reg))

#define COPY_ARG(arg) \
  if (arg < num_args) CopyOperand(frag, ni, call->arg_regs[arg], \
                                  call->args[arg])

#define MOVE_ARG(arg, reg) \
  if (arg < num_args) APP( \
     MOV_GPRv_GPRv_89(&ni, reg, call->arg_regs[arg]); \
     ni.ops[0].is_definition = true; )

#define RESTORE_ARG(arg, reg) \
  if (arg < num_args) \
    APP_INSTR(new AnnotationInstruction(IA_SSA_RESTORE_REG, reg))

// Generates some code to target some client function. The generated code tries
// to minimize the amount of saved/restored machine state, and punts on the
// virtual register system for the rest.
void ExtendFragmentWithInlineCall(ContextInterface *context,
                                  CodeFragment *frag,
                                  InlineFunctionCall *call) {
  auto ic = context->InlineCallback(call);
  auto num_args = call->NumArguments();
  GRANARY_ASSERT(nullptr != ic->wrapped_callback);

  Instruction ni;

  frag->attr.has_native_instrs = true;

  // Note: Separates the copying of (ops -> arg VRs) and (arg VRs -> arg GPRs)
  //       so that if the ops depend on the arg GPRs, then they won't be
  //       overwritten when storing the args. The extra redundancies are cleaned
  //       out by copy propagation + register scheduling.

  auto r15 = VirtualRegister::FromNative(XED_REG_R15);
  auto r14 = VirtualRegister::FromNative(XED_REG_R14);
  auto r13 = VirtualRegister::FromNative(XED_REG_R13);
  auto r12 = VirtualRegister::FromNative(XED_REG_R12);
  auto r11 = VirtualRegister::FromNative(XED_REG_R11);
  auto r10 = VirtualRegister::FromNative(XED_REG_R10);

  SAVE_ARG(0, r15);
  SAVE_ARG(1, r14);
  SAVE_ARG(2, r13);
  SAVE_ARG(3, r12);
  SAVE_ARG(4, r11);
  SAVE_ARG(5, r10);
  COPY_ARG(0);
  COPY_ARG(1);
  COPY_ARG(2);
  COPY_ARG(3);
  COPY_ARG(4);
  COPY_ARG(5);
  MOVE_ARG(0, r15);
  MOVE_ARG(1, r14);
  MOVE_ARG(2, r13);
  MOVE_ARG(3, r12);
  MOVE_ARG(4, r11);
  MOVE_ARG(5, r10);

  APP(CALL_NEAR_RELBRd(&ni, ic->wrapped_callback);
      ni.is_stack_blind = true;);

  RESTORE_ARG(5, r10);
  RESTORE_ARG(4, r11);
  RESTORE_ARG(3, r12);
  RESTORE_ARG(2, r13);
  RESTORE_ARG(1, r14);
  RESTORE_ARG(0, r15);

  delete call;
}

}  // namespace arch
}  // namespace granary
