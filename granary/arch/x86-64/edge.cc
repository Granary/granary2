/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/arch/driver.h"
#include "granary/arch/util.h"
#include "granary/arch/x86-64/builder.h"
#include "granary/arch/x86-64/slot.h"

#include "granary/base/base.h"
#include "granary/base/option.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"

#include "granary/code/edge.h"
#include "granary/code/fragment.h"

#include "granary/breakpoint.h"

#define ENC(...) \
  do { \
    __VA_ARGS__ ; \
    GRANARY_IF_DEBUG( auto ret = ) stage_enc.Encode(&ni, pc); \
    GRANARY_ASSERT(ret); \
    GRANARY_IF_DEBUG( ret = ) commit_enc.EncodeNext(&ni, &pc); \
    GRANARY_ASSERT(ret); \
  } while (0)

#define APP(edge, ...) \
  do { \
    __VA_ARGS__ ; \
    edge->instrs.Append(new NativeInstruction(&ni)); \
  } while (0)

extern "C" {

// The direct edge entrypoint code.
extern void granary_arch_enter_direct_edge(void);

}  // extern C

namespace granary {
namespace arch {
namespace {

static const auto kEnterDirect = granary_arch_enter_direct_edge;

}  // namespace

// Generates the direct edge entry code for getting onto a Granary private
// stack, disabling interrupts, etc.
//
// This code takes a pointer to the context so that the code generated will
// be able to pass the context pointer directly to `granary::EnterGranary`.
// This allows us to avoid saving the context pointer in the `DirectEdge`.
void GenerateDirectEdgeEntryCode(ContextInterface *context, CachePC pc) {
  Instruction ni;
  InstructionEncoder stage_enc(InstructionEncodeKind::STAGED);
  InstructionEncoder commit_enc(InstructionEncodeKind::COMMIT);
  GRANARY_IF_DEBUG( const auto start_pc = pc; )

  ENC(PUSHFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );
  GRANARY_IF_KERNEL( ENC(CLI(&ni)); )

  // Swap stacks. After swapping stacks, we are susceptible to re-entrancy
  // issues related to interrupts and signals.
  GRANARY_IF_KERNEL( ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(SLOT_PRIVATE_STACK),
                                        XED_REG_RSP)); )

  // Save `RSI` (arg 2 by Itanium ABI), and use `RSI` to pass the context into
  // `granary::EnterGranary`.
  ENC(PUSH_GPRv_50(&ni, XED_REG_RSI));
  ENC(MOV_GPRv_IMMz(&ni, XED_REG_RSI, reinterpret_cast<uintptr_t>(context)));

  // Transfer control to a generic Granary direct edge entrypoint. Try to be
  // smart about encoding the target.
  auto granary_entrypoint_pc = reinterpret_cast<CachePC>(kEnterDirect);
  auto diff = granary_entrypoint_pc - pc;
  if (0 > diff) diff = -diff;
  if (4294966272LL >= diff) {  // 2^32 - 1024.
    ENC(CALL_NEAR_RELBRd(&ni, granary_entrypoint_pc));
  } else {
    ENC(CALL_NEAR_MEMv(&ni, &kEnterDirect));
  }

  ENC(POP_GPRv_51(&ni, XED_REG_RSI));

  // Swap stacks. After swapping stacks, we are susceptible to re-entrancy
  // issues related to interrupts and signals.
  GRANARY_IF_KERNEL( ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(SLOT_PRIVATE_STACK),
                                        XED_REG_RSP)); )

  // Restore the flags, and potentially re-enable interrupts.
  ENC(POPFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

  // Return back into the edge code.
  ENC(RET_NEAR(&ni); ni.effective_operand_width = arch::ADDRESS_WIDTH_BITS; );

  GRANARY_ASSERT(arch::DIRECT_EDGE_CODE_SIZE_BYTES >= (pc - start_pc));
}

// Generates the direct edge code for a given `DirectEdge` structure.
void GenerateDirectEdgeCode(DirectEdge *edge, CachePC edge_entry_code) {
  Instruction ni;
  InstructionEncoder stage_enc(InstructionEncodeKind::STAGED);
  InstructionEncoder commit_enc(InstructionEncodeKind::COMMIT);
  auto pc = edge->edge_code;
  GRANARY_IF_DEBUG( const auto start_pc = pc; )

  // The first time this is executed, it will jump to the next instruction,
  // which also agrees with pretetching and predicting of unknown branches.
  // If profiling isn't enabled, then later executions will jump directly
  // to where they are meant to go.
  ENC(JMP_MEMv(&ni, &(edge->entry_target)));
  edge->entry_target = pc;  // `pc` is the address of the next instruction.

  // Slides the stack pointer down. Assumes a "safe" stack, regardless of
  // prior proof.
  if (arch::REDZONE_SIZE_BYTES) {
    ENC(LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                      BaseDispMemOp(-arch::REDZONE_SIZE_BYTES, XED_REG_RSP,
                                    arch::ADDRESS_WIDTH_BITS)));
  }

  // Steal `RDI` (arg1 on Itanium C++ ABI) to hold the address of the
  // `DirectEdge` data structure.
  ENC(PUSH_GPRv_50(&ni, XED_REG_RDI));
  ENC(MOV_GPRv_IMMz(&ni, XED_REG_RDI, reinterpret_cast<uintptr_t>(edge));
      if (16 >= ni.ops[1].width) { ni.ops[1].width = 32; } );

  // Call into the direct edge entry code, which might disable interrupts, and
  // will transfer control to a private stack.
  ENC(CALL_NEAR_RELBRd(&ni, edge_entry_code));

  // Restore the stolen `RDI`.
  ENC(POP_GPRv_51(&ni, XED_REG_RDI));

  // Slides the stack pointer down. Assumes a "safe" stack, regardless of
  // prior proof.
  if (arch::REDZONE_SIZE_BYTES) {
    ENC(LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                      BaseDispMemOp(arch::REDZONE_SIZE_BYTES, XED_REG_RSP,
                                    arch::ADDRESS_WIDTH_BITS)));
  }

  // Jump to the resolved PC, independent of profiling. As mentioned above,
  // if two or more threads are racing to translate a block, then the behavior
  // we'll observe is that one of them will "win" and the others will jump
  // back into the edge code because `meta->cache_pc` is initialized above
  // to point to into the edge code.
  ENC(JMP_MEMv(&ni, &(edge->exit_target)));

  // Make it so that the CPU doesn't prefetch after the `JMP`. It's typical for
  // the first execution of an indirect jump to predict the target as the next
  // instruction.
  ENC(UD2(&ni));

  GRANARY_ASSERT(arch::DIRECT_EDGE_CODE_SIZE_BYTES >= (pc - start_pc));
}

// Generates some indirect edge code that is used to look up the target of an
// indirect jump.
IndirectEdge *GenerateIndirectEdgeCode(ControlFlowInstruction *cfi,
                                       CodeFragment *in_edge,
                                       CodeFragment *out_edge) {
  GRANARY_ASSERT(!cfi->IsFunctionReturn());

  auto edge = new IndirectEdge;
  auto target_block = DynamicCast<InstrumentedBasicBlock *>(cfi->TargetBlock());
  GRANARY_ASSERT(nullptr != target_block);

  Instruction ni;

  // Get the target of the CFI into a register.
  const auto &target_op(cfi->instruction.ops[0]);
  GRANARY_ASSERT(target_op.IsRegister());  // Enforced by `1_mangle.cc`.

  // Manually save `RCX`.
  auto saved_rcx = target_block->cfg->AllocateVirtualRegister(GPR_WIDTH_BYTES);
  APP(in_edge, MOV_GPRv_GPRv_89(&ni, saved_rcx, XED_REG_RCX);
               ni.is_save_restore = true; );

  // Copy the target, just in case it's stored in `RCX`.
  auto saved_target = target_block->cfg->AllocateVirtualRegister(
      GPR_WIDTH_BYTES);
  APP(in_edge, MOV_GPRv_GPRv_89(&ni, saved_target, target_op.reg);
               ni.is_save_restore = true; );

  APP(in_edge, JMP_MEMv(&ni, &(edge->in_edge));
               ni.is_sticky = true; );
  in_edge->branch_instr = DynamicCast<NativeInstruction *>(
      in_edge->instrs.Last());
  APP(in_edge, UD2(&ni));

  auto compare_target = target_block->cfg->AllocateVirtualRegister(
      GPR_WIDTH_BYTES);
  APP(out_edge, MOV_GPRv_IMMz(&ni, compare_target, static_cast<uint64_t>(0)));
  APP(out_edge, LEA_GPRv_GPRv_GPRv(&ni, XED_REG_RCX, compare_target,
                                                     saved_target));
  auto hit = new LabelInstruction;
  APP(out_edge, JRCXZ_RELBRb(&ni, hit));

  // Restore `RCX`.
  out_edge->instrs.Append(hit);
  APP(out_edge, MOV_GPRv_GPRv_89(&ni, XED_REG_RCX, saved_rcx);
                ni.is_save_restore = true;);

  return edge;
}

}  // namespace arch
}  // namespace granary
