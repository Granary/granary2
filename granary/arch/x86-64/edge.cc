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
#include "granary/context.h"

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
extern void granary_arch_enter_indirect_edge(void);

}  // extern C

namespace granary {
namespace arch {
namespace {

static const auto kEnterDirect = granary_arch_enter_direct_edge;
static const auto kEnterIndirect = granary_arch_enter_indirect_edge;

static void Shorten_MOV_GPRv_IMMz(arch::Instruction *ni) {
  if (32 >= ni->ops[1].width) {
    ni->ops[0].width = 32;
    ni->ops[0].reg.Widen(4);
    ni->ops[1].width = 32;
  }
}
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
  GRANARY_IF_KERNEL( ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(SLOT_PRIVATE_STACK),
                                             XED_REG_RSP)); )

  // Save `RSI` (arg 2 by Itanium ABI), and use `RSI` to pass the context into
  // `granary::EnterGranary`.
  ENC(PUSH_GPRv_50(&ni, XED_REG_RSI));
  ENC(MOV_GPRv_IMMz(&ni, XED_REG_RSI, reinterpret_cast<uintptr_t>(context));
      Shorten_MOV_GPRv_IMMz(&ni););

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

  // Restore the flags, and potentially re-enable interrupts.
  GRANARY_IF_KERNEL( ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(SLOT_PRIVATE_STACK),
                                             XED_REG_RSP)); )
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

  GRANARY_IF_USER(ENC(LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                                         BaseDispMemOp(-REDZONE_SIZE_BYTES,
                                                       XED_REG_RSP,
                                                       ADDRESS_WIDTH_BITS))));
  // Steal `RDI` (arg1 on Itanium C++ ABI) to hold the address of the
  // `DirectEdge` data structure.
  ENC(PUSH_GPRv_50(&ni, XED_REG_RDI));
  ENC(MOV_GPRv_IMMz(&ni, XED_REG_RDI, reinterpret_cast<uintptr_t>(edge));
      Shorten_MOV_GPRv_IMMz(&ni););

  // Call into the direct edge entry code, which might disable interrupts, and
  // will transfer control to a private stack.
  ENC(CALL_NEAR_RELBRd(&ni, edge_entry_code));

  // Restore the stolen `RDI`.
  ENC(POP_GPRv_51(&ni, XED_REG_RDI));

  // Restore back to the native stack.
  GRANARY_IF_USER(ENC(LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                                         BaseDispMemOp(REDZONE_SIZE_BYTES,
                                                       XED_REG_RSP,
                                                       ADDRESS_WIDTH_BITS))));

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

// Generates the indirect edge entry code for getting onto a Granary private
// stack, disabling interrupts, etc.
//
// This code takes a pointer to the context so that the code generated will
// be able to pass the context pointer directly to `granary::EnterGranary`.
// This allows us to avoid saving the context pointer in the `IndirectEdge`.
void GenerateIndirectEdgeEntryCode(ContextInterface *context, CachePC pc) {
  Instruction ni;
  InstructionEncoder stage_enc(InstructionEncodeKind::STAGED);
  InstructionEncoder commit_enc(InstructionEncodeKind::COMMIT);
  GRANARY_IF_DEBUG( const auto start_pc = pc; )

  // Swap stacks. After swapping stacks, we are susceptible to re-entrancy
  // issues related to interrupts and signals.
  GRANARY_IF_USER(ENC(LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                                         BaseDispMemOp(-REDZONE_SIZE_BYTES,
                                                       XED_REG_RSP,
                                                       ADDRESS_WIDTH_BITS))));
  ENC(PUSHFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );
  GRANARY_IF_KERNEL( ENC(CLI(&ni)); )
  GRANARY_IF_KERNEL( ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(SLOT_PRIVATE_STACK),
                                             XED_REG_RSP)); )

  // Save `RSI` (arg 2 by Itanium ABI), and use `RSI` to pass the context into
  // `granary::EnterGranary`. `RDI` already holds the address of the
  // `IndirectEdge`, and `RCX` holds the native address of the target block.
  ENC(PUSH_GPRv_50(&ni, XED_REG_RSI));
  ENC(MOV_GPRv_IMMz(&ni, XED_REG_RSI, reinterpret_cast<uintptr_t>(context));
      Shorten_MOV_GPRv_IMMz(&ni););

  // Transfer control to a generic Granary direct edge entrypoint. Try to be
  // smart about encoding the target.
  auto granary_entrypoint_pc = reinterpret_cast<CachePC>(kEnterIndirect);
  auto diff = granary_entrypoint_pc - pc;
  if (0 > diff) diff = -diff;
  if (4294966272LL >= diff) {  // 2^32 - 1024.
    ENC(CALL_NEAR_RELBRd(&ni, granary_entrypoint_pc));
  } else {
    ENC(CALL_NEAR_MEMv(&ni, &kEnterIndirect));
  }

  ENC(POP_GPRv_51(&ni, XED_REG_RSI));

  GRANARY_IF_KERNEL( ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(SLOT_PRIVATE_STACK),
                                             XED_REG_RSP)); )

  // Restore the flags, and potentially re-enable interrupts. After this
  // instruction, it is fairly likely that we will hit an interrupt.
  ENC(POPFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

  // Swap back to the native stack.
  GRANARY_IF_USER(ENC(LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                                         BaseDispMemOp(REDZONE_SIZE_BYTES,
                                                       XED_REG_RSP,
                                                       ADDRESS_WIDTH_BITS))));

  // Return back into the in-edge code.
  ENC(JMP_MEMv(&ni, BaseDispMemOp(offsetof(IndirectEdge, out_edge_pc),
                                  XED_REG_RDI, arch::ADDRESS_WIDTH_BITS)));

  ENC(UD2(&ni));

  GRANARY_ASSERT(arch::INDIRECT_EDGE_CODE_SIZE_BYTES >= (pc - start_pc));
}

// Generates some indirect edge code that is used to look up the target of an
// indirect jump.
void GenerateIndirectEdgeCode(IndirectEdge *edge,
                              ControlFlowInstruction *cfi,
                              CodeFragment *in_edge,
                              CodeFragment *out_edge_miss,
                              CodeFragment *out_edge_hit,
                              ExitFragment *out_edge_exit) {
  GRANARY_ASSERT(!cfi->IsFunctionReturn());

  auto target_block = DynamicCast<InstrumentedBasicBlock *>(cfi->TargetBlock());
  auto cfg = target_block->cfg;

  Instruction ni;

  // Get the target of the CFI into a register.
  const auto &target_op(cfi->instruction.ops[0]);
  GRANARY_ASSERT(target_op.IsRegister());  // Enforced by `1_mangle.cc`.

  auto begin_template = new AnnotationInstruction(
      IA_UPDATE_ENCODED_ADDRESS, &(edge->begin_out_edge_template));
  auto end_template = new AnnotationInstruction(
      IA_UPDATE_ENCODED_ADDRESS, &(edge->end_out_edge_template));

  // Manually save `RCX`.
  auto saved_rcx = cfg->AllocateVirtualRegister(GPR_WIDTH_BYTES);
  auto saved_rdi = cfg->AllocateVirtualRegister(GPR_WIDTH_BYTES);
  APP(in_edge, MOV_GPRv_GPRv_89(&ni, saved_rcx, XED_REG_RCX));

  // Copy the target, just in case it's stored in `RCX` or `RDI`.
  auto saved_target = target_op.reg;
  if (VirtualRegister::FromNative(XED_REG_RCX) == saved_target) {
    saved_target = saved_rcx;
  } else if (VirtualRegister::FromNative(XED_REG_RDI) == saved_target) {
    saved_target = saved_rdi;
  }

  // Store the pointer to the `IndirectEdge` data structure in `RDI`
  // (arg1 of the Itanium C++ ABI).
  APP(in_edge, MOV_GPRv_GPRv_89(&ni, saved_rdi, XED_REG_RDI));
  APP(in_edge, MOV_GPRv_IMMz(&ni, XED_REG_RDI,
                                  reinterpret_cast<uint64_t>(edge)));
  APP(in_edge, JMP_MEMv(&ni, BaseDispMemOp(offsetof(IndirectEdge, out_edge_pc),
                                           XED_REG_RDI,
                                           arch::ADDRESS_WIDTH_BITS));
               ni.is_sticky = true; );
  in_edge->branch_instr = DynamicCast<NativeInstruction *>(
      in_edge->instrs.Last());

  // First execution of the indirect jump will target this label, which will
  // lead to a context switch into Granary.
  auto miss = new LabelInstruction();
  auto miss_addr = new AnnotationInstruction(IA_UPDATE_ENCODED_ADDRESS,
                                             &(edge->out_edge_pc));
  in_edge->instrs.Append(miss);
  in_edge->instrs.Append(miss_addr);

  // Store the branch target into `RCX`. The address of the `IndirectEdge`
  // data structure remains in `RDI`. Jump to `edge->in_edge_pc`, which is
  // initialized to be
  APP(out_edge_miss, MOV_GPRv_GPRv_89(&ni, XED_REG_RCX, saved_target); );
  APP(out_edge_miss, JMP_RELBRd(&ni, edge->out_edge_pc);
                     ni.is_sticky = true; );
  out_edge_miss->branch_instr = DynamicCast<NativeInstruction *>(
      out_edge_miss->instrs.Last());
  APP(out_edge_miss, UD2(&ni));

  out_edge_hit->instrs.Append(begin_template);

  // Gets updated later by:
  //    1)  Moving the target of the control-flow instruction into `RCX`
  //        (first instruction).
  //    2)  Jumping directly to the targeted basic block (last instruction).

  APP(out_edge_hit, LEA_GPRv_GPRv_GPRv(&ni, XED_REG_RCX, XED_REG_RCX,
                                                         saved_target));
  auto hit = new LabelInstruction;
  APP(out_edge_hit, JRCXZ_RELBRb(&ni, hit));

  // Go back into Granary.
  APP(out_edge_hit, JMP_RELBRd(&ni, miss));
  out_edge_hit->branch_instr = DynamicCast<NativeInstruction *>(
      out_edge_hit->instrs.Last());

  // Manually restore `RCX` and `RDI`.
  out_edge_hit->instrs.Append(hit);
  APP(out_edge_hit, MOV_GPRv_GPRv_89(&ni, XED_REG_RCX, saved_rcx));
  APP(out_edge_hit, MOV_GPRv_GPRv_89(&ni, XED_REG_RDI, saved_rdi));

  out_edge_exit->instrs.Append(end_template);
  APP(out_edge_exit, UD2(&ni));

  // Don't surround this code in flag save fragments as we don't modify the
  // flags.
  in_edge->attr.is_app_code = true;
  out_edge_miss->attr.is_app_code = true;
  out_edge_hit->attr.is_app_code = true;
}

enum {
  JMP_RELBRd_SIZE_BYTES = 5
};

// Instantiate an indirect out-edge template. The indirect out-edge will
// compare the target of a CFI with `app_pc`, and if the values match, then
// will jump to `cache_pc`, otherwise a fall-back is taken.
//
// Note: This function is protected by `Context::indirect_edge_list_lock`.
void InstantiateIndirectEdge(IndirectEdge *edge, CachePC edge_pc,
                             AppPC app_pc, CachePC cache_pc) {
  auto pc = edge_pc;
  InstructionEncoder stage_enc(InstructionEncodeKind::STAGED);
  InstructionEncoder commit_enc(InstructionEncodeKind::COMMIT);
  InstructionDecoder decoder;
  Instruction ni;

  // Replace the `IndirectEdge::out_edge_pc` with the out edge that we're
  // creating, and make our new out edge point to the old one.
  auto miss_pc = edge->out_edge_pc;
  edge->out_edge_pc = edge_pc;

  // Negate the pointer, so that when it's added to its non-negated self, they
  // cancel out and trigger the `JRCXZ`.
  ENC(MOV_GPRv_IMMz(&ni, XED_REG_RCX, -reinterpret_cast<intptr_t>(app_pc));
      Shorten_MOV_GPRv_IMMz(&ni));

  for (auto template_pc(edge->begin_out_edge_template);
       template_pc < edge->end_out_edge_template; ) {
    decoder.DecodeNext(&ni, &template_pc);
    if (XED_IFORM_JMP_RELBRd == ni.iform) {
      ni.SetBranchTarget(miss_pc);  // Miss! Jump to fall-back.
    } else if (XED_ICLASS_JRCXZ == ni.iclass) {  // Need to relativize.
      ni.SetBranchTarget(pc + ni.decoded_length + JMP_RELBRd_SIZE_BYTES);
    }
    ENC();
  }
  ENC(JMP_RELBRd(&ni, cache_pc));  // Hit! Jump to block.
}

}  // namespace arch
}  // namespace granary
