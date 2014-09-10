/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/driver.h"
#include "arch/util.h"
#include "arch/x86-64/builder.h"
#include "arch/x86-64/slot.h"

#include "granary/base/base.h"
#include "granary/base/option.h"

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"

#include "granary/code/edge.h"
#include "granary/code/fragment.h"
#include "granary/code/metadata.h"

#include "granary/breakpoint.h"
#include "granary/cache.h"
#include "granary/context.h"

enum {
  // Should we use an NMI to get into Granary instead of a call and switch
  // stacks? This should be `0` as it was somewhat of a failed experiment.
  USE_NMI = 0
};

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

// Granary's `granary_nmi_edge_handler` uses these to distinguish between
// a real NMI, an NMI from an indirect edge, and an NMI from a direct edge.
extern granary::CachePC granary_direct_edge_return_rip;
extern granary::CachePC granary_indirect_edge_return_rip;

}  // extern C

namespace granary {
namespace arch {
namespace {

static const auto kEnterDirect = granary_arch_enter_direct_edge;
static const auto kEnterIndirect = granary_arch_enter_indirect_edge;

// TODO(pag): Potential leak.
static NativeAddress *enter_direct_addr = nullptr;
static NativeAddress *enter_indirect_addr = nullptr;

static void CALL_NEAR(arch::Instruction *ni, CachePC pc, AppPC target_pc,
                      NativeAddress **na) {
  auto diff = target_pc - pc;
  if (0 > diff) diff = -diff;
  if (4294966272LL >= diff) {  // 2^32 - 1024.
    CALL_NEAR_RELBRd(ni, target_pc);
  } else {
    if (!*na) {
      new NativeAddress(target_pc, na);
    }
    CALL_NEAR_MEMv(ni, &((*na)->addr));
  }
}

// Used to make a move of an address smaller. This is only really helpful in
// user space.
static void Shorten_MOV_GPRv_IMMv(arch::Instruction *ni) {
  if (32 >= ni->ops[1].width) {
    ni->ops[0].width = 32;
    ni->ops[0].reg.Widen(4);
    ni->ops[1].width = 32;
  }
}

// Helps us distinguish call going through an edge from an un/conditional
// jump.
static bool TargetStackIsValid(const DirectEdge *edge) {
  const auto target_meta = MetaDataCast<StackMetaData *>(
      edge->dest_meta.load(std::memory_order_relaxed));
  return target_meta->has_stack_hint && target_meta->behaves_like_callstack;
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

  // Save the flags.
  ENC(PUSHFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

  // Disable interrupts and swap stacks.
  if (!USE_NMI GRANARY_IF_USER( && false)) {
    ENC(CLI(&ni));
    ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(os::SLOT_PRIVATE_STACK), XED_REG_RSP));
  }

  // Save `RSI` (arg 2 by Itanium ABI), and use `RSI` to pass the context into
  // `granary::EnterGranary`.
  ENC(PUSH_GPRv_50(&ni, XED_REG_RSI));
  ENC(MOV_GPRv_IMMv(&ni, XED_REG_RSI, reinterpret_cast<uintptr_t>(context));
      Shorten_MOV_GPRv_IMMv(&ni););

  // Transfer control to a generic Granary direct edge entrypoint. Try to be
  // smart about encoding the target.
  if (USE_NMI) {
    ENC(INT_IMMb(&ni, static_cast<uint8_t>(2)));  // Raise an NMI.
    granary_direct_edge_return_rip = pc;
  } else {
    auto granary_entrypoint_pc = reinterpret_cast<CachePC>(kEnterDirect);
    ENC(CALL_NEAR(&ni, pc, granary_entrypoint_pc, &enter_direct_addr));
  }

  ENC(POP_GPRv_51(&ni, XED_REG_RSI));

  // Swap stacks.
  if (!USE_NMI GRANARY_IF_USER( && false)) {
    ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(os::SLOT_PRIVATE_STACK), XED_REG_RSP));
  }

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
  auto target_stack_valid = TargetStackIsValid(edge);
  GRANARY_IF_DEBUG( const auto start_pc = pc; )

  // The first time this is executed, it will jump to the next instruction,
  // which also agrees with prefetching and predicting of unknown branches.
  //
  // If profiling isn't enabled, then later executions will jump directly
  // to where they are meant to go.
  //
  // Another benefit to this approach is that if patching is not enabled, then
  // Granary's code cache is append-only, meaning that it can (in theory)
  // instrument itself without having to support SMC.
  ENC(JMP_MEMv(&ni, &(edge->entry_target)));
  edge->entry_target = pc;  // `pc` is the address of the next instruction.

  if (REDZONE_SIZE_BYTES && !target_stack_valid) {
    ENC(LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                           BaseDispMemOp(-REDZONE_SIZE_BYTES,
                                         XED_REG_RSP,
                                         ADDRESS_WIDTH_BITS)));
  }

  // Steal `RDI` (arg1 on Itanium C++ ABI) to hold the address of the
  // `DirectEdge` data structure.
  ENC(PUSH_GPRv_50(&ni, XED_REG_RDI));
  ENC(MOV_GPRv_IMMv(&ni, XED_REG_RDI, reinterpret_cast<uintptr_t>(edge));
      Shorten_MOV_GPRv_IMMv(&ni););

  // Call into the direct edge entry code, which might disable interrupts, and
  // will transfer control to a private stack.
  ENC(CALL_NEAR_RELBRd(&ni, edge_entry_code));

  // Restore the stolen `RDI`.
  ENC(POP_GPRv_51(&ni, XED_REG_RDI));

  // Restore back to the native stack.
  if (REDZONE_SIZE_BYTES && !target_stack_valid) {
    ENC(LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                           BaseDispMemOp(REDZONE_SIZE_BYTES,
                                         XED_REG_RSP,
                                         ADDRESS_WIDTH_BITS)));
  }

  // Jump to the resolved PC, independent of profiling. As mentioned above,
  // if two or more threads are racing to translate a block, then the behavior
  // we'll observe is that one of them will "win" and the others will jump
  // back into the edge code because `edge->exit_target` is initialized
  // above to point to into the edge code.
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

  // Save the flags and potentially disable interrupts.
  ENC(PUSHFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

  if (!USE_NMI GRANARY_IF_USER( && false)) {
    // Disable interrupts and swap onto Granary's private stack.
    ENC(CLI(&ni));
    ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(os::SLOT_PRIVATE_STACK), XED_REG_RSP));
  }

  // Save `RSI` (arg 2 by Itanium ABI), and use `RSI` to pass the context into
  // `granary::EnterGranary`. `RDI` already holds the address of the
  // `IndirectEdge`, and `RCX` holds the native address of the target block.
  ENC(PUSH_GPRv_50(&ni, XED_REG_RSI));
  ENC(MOV_GPRv_IMMv(&ni, XED_REG_RSI, reinterpret_cast<uintptr_t>(context));
      Shorten_MOV_GPRv_IMMv(&ni););

  // Transfer control to a generic Granary direct edge entrypoint. Try to be
  // smart about encoding the target.
  if (USE_NMI) {
    ENC(INT_IMMb(&ni, static_cast<uint8_t>(2)));  // Raise an NMI.
    granary_indirect_edge_return_rip = pc;
  } else {
    auto granary_entrypoint_pc = reinterpret_cast<CachePC>(kEnterIndirect);
    ENC(CALL_NEAR(&ni, pc, granary_entrypoint_pc, &enter_indirect_addr));
  }

  ENC(POP_GPRv_51(&ni, XED_REG_RSI));

  if (!USE_NMI GRANARY_IF_USER( && false)) {
    // Swap back to the native stack.
    ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(os::SLOT_PRIVATE_STACK), XED_REG_RSP));
  }

  // Restore the flags, and potentially re-enable interrupts. After this
  // instruction, it is reasonably likely that we will hit an interrupt.
  ENC(POPFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

  // Return back into the in-edge code.
  ENC(JMP_MEMv(&ni, BaseDispMemOp(offsetof(IndirectEdge, out_edge_pc),
                                  XED_REG_RDI, arch::ADDRESS_WIDTH_BITS)));

  ENC(UD2(&ni));

  GRANARY_ASSERT(arch::INDIRECT_EDGE_CODE_SIZE_BYTES >= (pc - start_pc));
}

// Update the attribute info of an indirect edge fragment.
static void UpdateIndirectEdgeFrag(CodeFragment *edge_frag,
                                   CodeFragment *pred_frag,
                                   BlockMetaData *dest_block_meta) {
  edge_frag->attr.block_meta = dest_block_meta;

  // Prevent this fragment from being reaped by `RemoveUselessFrags` in
  // `3_partition_fragments.cc`.
  edge_frag->attr.has_native_instrs = true;

  // Don't surround this code in flag save fragments as we don't modify the
  // flags.
  edge_frag->type = CODE_TYPE_APP;

  // Make sure that the edge code shares the same partition as the predecessor
  // so that virtual registers can be spread across both.
  edge_frag->attr.can_add_succ_to_partition = true;
  edge_frag->partition.Union(edge_frag, pred_frag);
}

// Generates some indirect edge code that is used to look up the target of an
// indirect jump.
//
// We generate the following structure:
//
//               in_edge ----.-> go_to_granary
//                 |         |       |
//          compare_target --' <-----'
//                 |
//            exit_to_block
//
CodeFragment *GenerateIndirectEdgeCode(FragmentList *frags, IndirectEdge *edge,
                                       ControlFlowInstruction *cfi,
                                       CodeFragment *predecessor_frag,
                                       BlockMetaData *dest_block_meta) {
  GRANARY_ASSERT(!cfi->IsFunctionReturn());

  auto in_edge = new CodeFragment;
  auto go_to_granary = new CodeFragment;
  auto compare_target = new CodeFragment;
  auto exit_to_block = new ExitFragment(FRAG_EXIT_FUTURE_BLOCK_INDIRECT);
  auto is_call_ret = cfi->IsFunctionCall() ||
                     IsA<ReturnBasicBlock *>(cfi->TargetBlock());

  // Set up the edges. Some of these are "sort of" lies, in the sense that
  // we will often use the combination of a `branch_instr` and
  // `FRAG_SUCC_BRANCH` to trick `10_add_connecting_jumps.cc` to put the
  // fragments in the desired order.
  in_edge->successors[FRAG_SUCC_FALL_THROUGH] = go_to_granary;
  in_edge->successors[FRAG_SUCC_BRANCH] = compare_target;
  go_to_granary->successors[FRAG_SUCC_BRANCH] = compare_target;
  compare_target->successors[FRAG_SUCC_FALL_THROUGH] = exit_to_block;
  compare_target->successors[FRAG_SUCC_BRANCH] = go_to_granary;

  exit_to_block->edge.kind = EDGE_KIND_INDIRECT;
  exit_to_block->block_meta = dest_block_meta;

  // Add the fragments, and set some of their attributes.
  frags->Append(in_edge);
  frags->Append(go_to_granary);
  frags->Append(compare_target);
  frags->Append(exit_to_block);

  UpdateIndirectEdgeFrag(in_edge, predecessor_frag, dest_block_meta);
  UpdateIndirectEdgeFrag(go_to_granary, predecessor_frag, dest_block_meta);
  UpdateIndirectEdgeFrag(compare_target, predecessor_frag, dest_block_meta);

  in_edge->attr.is_in_edge_code = true;

  Instruction ni;

  // Get the target of the CFI into a register.
  const auto &target_op(cfi->instruction.ops[0]);
  GRANARY_ASSERT(target_op.IsRegister());  // Enforced by `1_mangle.cc`.

  // --------------------- in_edge --------------------------------

  if (REDZONE_SIZE_BYTES && !is_call_ret) {
    APP(in_edge,
        LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                           BaseDispMemOp(-REDZONE_SIZE_BYTES,
                                         XED_REG_RSP,
                                         ADDRESS_WIDTH_BITS));
        ni.is_stack_blind = true;
        ni.analyzed_stack_usage = false; );
  }

  // Copy the target, just in case it's stored in `RCX` or `RDI`.
  auto cfi_target = target_op.reg;
  auto restore_rdx = false;
  if (VirtualRegister::FromNative(XED_REG_RCX) == cfi_target ||
      VirtualRegister::FromNative(XED_REG_RDI) == cfi_target) {
    APP(in_edge, PUSH_GPRv_50(&ni, XED_REG_RDX);
                 ni.is_stack_blind = true;
                 ni.analyzed_stack_usage = false; );
    APP(in_edge, MOV_GPRv_GPRv_89(&ni, XED_REG_RDX, cfi_target); );
    cfi_target = VirtualRegister::FromNative(XED_REG_RDX);
    restore_rdx = true;
  }

  // Spill `RCX` and `RDI` on the stack. If the stack isn't valid in this
  // partition, then the stack pointer should have already been shifted.
  APP(in_edge, PUSH_GPRv_50(&ni, XED_REG_RCX);
               ni.is_stack_blind = true;
               ni.analyzed_stack_usage = false; );
  APP(in_edge, PUSH_GPRv_50(&ni, XED_REG_RDI);
               ni.is_stack_blind = true;
               ni.analyzed_stack_usage = false; );

  // Store the pointer to the `IndirectEdge` data structure in `RDI`
  // (arg1 of the Itanium C++ ABI).
  APP(in_edge, MOV_GPRv_IMMv(&ni, XED_REG_RDI,
                                  reinterpret_cast<uint64_t>(edge)));
  APP(in_edge, JMP_MEMv(&ni, BaseDispMemOp(offsetof(IndirectEdge, out_edge_pc),
                                           XED_REG_RDI,
                                           arch::ADDRESS_WIDTH_BITS));
               ni.is_sticky = true; );
  in_edge->branch_instr = DynamicCast<NativeInstruction *>(
      in_edge->instrs.Last());

  // First execution of the indirect jump will target this label, which will
  // lead to a context switch into Granary.
  auto back_to_granary = new LabelInstruction;
  in_edge->instrs.Append(back_to_granary);

  // For the fall-through; want to make sure no weird register allocation
  // stuff gets in the way.
  auto miss_addr = new AnnotationInstruction(IA_UPDATE_ENCODED_ADDRESS,
                                             &(edge->out_edge_pc));
  in_edge->instrs.Append(miss_addr);

  // --------------------- go_to_granary --------------------------------

  // Store the branch target into `RCX`. The address of the `IndirectEdge`
  // data structure remains in `RDI`. Jump to `edge->in_edge_pc`, which is
  // initialized to be the indirect edge entrypoint edge code.
  APP(go_to_granary, MOV_GPRv_GPRv_89(&ni, XED_REG_RCX, cfi_target); );
  APP(go_to_granary, JMP_RELBRd(&ni, edge->out_edge_pc);
                     ni.is_sticky = true; );
  go_to_granary->branch_instr = DynamicCast<NativeInstruction *>(
      go_to_granary->instrs.Last());
  APP(go_to_granary, UD2(&ni));

  auto begin_template = new AnnotationInstruction(
      IA_UPDATE_ENCODED_ADDRESS, &(edge->out_edge_template));
  go_to_granary->instrs.Append(begin_template);

  // --------------------- compare_target --------------------------------

  // Gets updated later by:
  //    1)  Moving the target of the control-flow instruction into `RCX`
  //        (first instruction).
  //    2)  Jumping directly to the targeted basic block (last instruction).
  APP(compare_target, MOV_GPRv_IMMv(&ni, XED_REG_RCX, 0UL);
                      ni.dont_encode = true; );
  APP(compare_target, LEA_GPRv_GPRv_GPRv(&ni, XED_REG_RCX, XED_REG_RCX,
                                              cfi_target));
  auto go_to_exit_to_block = new LabelInstruction;

  // Note: We add the `JRCXZ` as the branch instruction, as opposed to the
  //       next `JMP_RELBRd` (which should be the `branch_instr`) because then
  //       later stages will see the `JRCXZ` as conditional, and propagate
  //       registers / flags correctly.
  APP(compare_target, JRCXZ_RELBRb(&ni, go_to_exit_to_block));
  compare_target->branch_instr = DynamicCast<NativeInstruction *>(
      compare_target->instrs.Last());
  APP(compare_target, JMP_RELBRd(&ni, back_to_granary); ni.is_sticky = true; );

  // LABEL: `go_to_exit_to_block`.
  compare_target->instrs.Append(go_to_exit_to_block);

  APP(compare_target, POP_GPRv_51(&ni, XED_REG_RDI);
                      ni.is_stack_blind = true;
                      ni.analyzed_stack_usage = false; );
  APP(compare_target, POP_GPRv_51(&ni, XED_REG_RCX);
                      ni.is_stack_blind = true;
                      ni.analyzed_stack_usage = false; );
  if (restore_rdx) {
    APP(compare_target, POP_GPRv_51(&ni, XED_REG_RDX);
                        ni.is_stack_blind = true;
                        ni.analyzed_stack_usage = false; );
  }

  if (REDZONE_SIZE_BYTES && !is_call_ret) {
    APP(compare_target,
        LEA_GPRv_AGEN(&ni, XED_REG_RSP,
                           BaseDispMemOp(REDZONE_SIZE_BYTES,
                                         XED_REG_RSP,
                                         ADDRESS_WIDTH_BITS));
        ni.is_stack_blind = true;
        ni.analyzed_stack_usage = false; );
  }

  // --------------------- exit_to_block --------------------------------

  APP(exit_to_block, UD2(&ni));

  return in_edge;
}

enum {
  JMP_RELBRd_SIZE_BYTES = 5
};

// Instantiate an indirect out-edge template. The indirect out-edge will
// compare the target of a CFI with `app_pc`, and if the values match, then
// will jump to `cache_pc`, otherwise a fall-back is taken.
//
// This function works by prepending a dummy fragment to `frags`, where the
// instructions of the fragment
//
// Note: This function must be called in the context of an
//       `IndirectEdge::out_edge_pc_lock`.
void InstantiateIndirectEdge(IndirectEdge *edge, FragmentList *frags,
                             AppPC app_pc) {
  InstructionDecoder decoder;
  Instruction ni;
  Instruction mov;

  auto first_frag = frags->First();
  auto frag = new Fragment;
  frags->Prepend(frag);
  frag->next = first_frag;
  frag->successors[FRAG_SUCC_FALL_THROUGH] = first_frag;

  // Replace the `IndirectEdge::out_edge_pc` with the out edge that we're
  // creating, and make our new out edge point to the old one.
  auto new_out_edge_pc = new AnnotationInstruction(IA_UPDATE_ENCODED_ADDRESS,
                                                   &(edge->out_edge_pc));
  frag->instrs.Append(new_out_edge_pc);

  BranchInstruction *jrcxz(nullptr);
  AppPC jrcxz_target(nullptr);

  GRANARY_IF_DEBUG( auto added_lea = false;
                    auto found_jrcxz_target = false; )
  for (auto pc = edge->out_edge_template; decoder.DecodeNext(&ni, &pc); ) {

    // Look for the `LEA` that adds the address to its complement, and then
    // inject the move of the complemented address before the `LEA`.
    if (XED_ICLASS_LEA == ni.iclass &&
        VirtualRegister::FromNative(XED_REG_RCX) == ni.ops[0].reg) {

      // Negate the pointer, so that when it's added to its non-negated self,
      // they cancel out and trigger the `JRCXZ`.
      MOV_GPRv_IMMv(&mov, XED_REG_RCX, -reinterpret_cast<intptr_t>(app_pc));
      Shorten_MOV_GPRv_IMMv(&mov);
      frag->instrs.Append(new NativeInstruction(&mov));

      GRANARY_IF_DEBUG( added_lea = true; )

    } else if (XED_ICLASS_JRCXZ == ni.iclass) {  // Need to relativize.
      jrcxz = new BranchInstruction(&ni, new LabelInstruction);
      jrcxz_target = ni.BranchTargetPC();
      frag->instrs.Append(jrcxz);
      continue;

    } else if (XED_IFORM_JMP_RELBRd == ni.iform) {
      ni.SetBranchTarget(edge->out_edge_pc);  // Miss! Jump to fall-back.

    // Modify the target of the `JRCXZ`.
    } else if (jrcxz_target == ni.DecodedPC()) {
      GRANARY_ASSERT(nullptr != jrcxz);
      frag->instrs.Append(jrcxz->TargetLabel());
      GRANARY_IF_DEBUG( found_jrcxz_target = true; )
    }
    APP(frag);
  }

  GRANARY_ASSERT(nullptr != jrcxz_target);
  GRANARY_ASSERT(added_lea);
  GRANARY_ASSERT(found_jrcxz_target);
}

}  // namespace arch
}  // namespace granary
