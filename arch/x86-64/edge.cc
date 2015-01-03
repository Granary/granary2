/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "arch/driver.h"
#include "arch/util.h"

#include "arch/x86-64/builder.h"
#include "arch/x86-64/slot.h"
#include "arch/x86-64/register.h"

#include "granary/base/base.h"

#include "granary/cfg/block.h"
#include "granary/cfg/trace.h"
#include "granary/cfg/instruction.h"

#include "granary/code/edge.h"
#include "granary/code/fragment.h"
#include "granary/code/metadata.h"

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

#define APP(edge, ...) \
  do { \
    __VA_ARGS__ ; \
    edge->instrs.Append(new NativeInstruction(&ni)); \
  } while (0)

extern "C" {

// The direct edge entrypoint code.
extern const unsigned char granary_arch_enter_direct_edge;
extern const unsigned char granary_arch_enter_indirect_edge;

}  // extern C

namespace granary {
namespace arch {
namespace {

// Helps us distinguish call going through an edge from an un/conditional
// jump.
static bool TargetStackIsValid(const DirectEdge *edge) {
  const auto target_meta = MetaDataCast<StackMetaData *>(edge->dest_meta);
  return target_meta->has_stack_hint && target_meta->behaves_like_callstack;
}

}  // namespace

// Generates the direct edge entry code for getting onto a Granary private
// stack, disabling interrupts, etc.
//
// This code takes a pointer to the context so that the code generated will
// be able to pass the context pointer directly to `granary::EnterGranary`.
// This allows us to avoid saving the context pointer in the `DirectEdge`.
void GenerateDirectEdgeEntryCode(CachePC pc) {
  Instruction ni;
  InstructionEncoder stage_enc(InstructionEncodeKind::STAGED);
  InstructionEncoder commit_enc(InstructionEncodeKind::COMMIT);
  GRANARY_IF_DEBUG( const auto start_pc = pc; )

  // Save the flags.
  ENC(PUSHFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

  // Disable interrupts and swap stacks.
  if (GRANARY_IF_USER_ELSE(false, true)) {
    ENC(CLI(&ni));
    ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(os::SLOT_PRIVATE_STACK), XED_REG_RSP));
  }

  // Transfer control to a generic Granary direct edge entrypoint. Try to be
  // smart about encoding the target.
  ENC(CALL_NEAR_RELBRd(&ni, &granary_arch_enter_direct_edge));

  // Swap stacks.
  if (GRANARY_IF_USER_ELSE(false, true)) {
    ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(os::SLOT_PRIVATE_STACK), XED_REG_RSP));
  }

  // Restore the flags, and potentially re-enable interrupts.
  ENC(POPFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

  // Return back into the edge code.
  ENC(RET_NEAR(&ni); ni.effective_operand_width = arch::ADDRESS_WIDTH_BITS; );

  GRANARY_ASSERT(arch::DIRECT_EDGE_ENTRY_CODE_SIZE_BYTES >= (pc - start_pc));
}

// Generates the direct edge code for a given `DirectEdge` structure.
void GenerateDirectEdgeCode(DirectEdge *edge) {
  Instruction ni;
  InstructionEncoder stage_enc(InstructionEncodeKind::STAGED);
  InstructionEncoder commit_enc(InstructionEncodeKind::COMMIT);
  auto pc = edge->edge_code_pc;
  auto target_stack_valid = TargetStackIsValid(edge);
  GRANARY_IF_DEBUG( const auto start_pc = pc; )

  // Clear the memory with `INT3`s.
  memset(pc, 0xCC, arch::DIRECT_EDGE_CODE_SIZE_BYTES);

  // The first time this is executed, it will jump to the next instruction,
  // which also agrees with prefetching and predicting of unknown branches.
  // After the target block is translated, we will update `entry_target_pc`
  // to point to the new block so that later executions will jump directly
  // to where they are meant to go.
  //
  // Another benefit to this approach is that if patching is not enabled, then
  // Granary's code cache is append-only, meaning that it can (in theory)
  // instrument itself without having to support SMC.
  ENC(JMP_MEMv(&ni, &(edge->entry_target_pc)));
  edge->entry_target_pc = pc;  // `pc` is the address of the next instruction.

  if (REDZONE_SIZE_BYTES && !target_stack_valid) {
    ENC(SHIFT_REDZONE(&ni));
  }

  // Steal `RDI` (arg1 on Itanium C++ ABI) to hold the address of the
  // `DirectEdge` data structure.
  ENC(PUSH_GPRv_50(&ni, XED_REG_RDI));
  ENC(MOV_GPRv_IMMv(&ni, XED_REG_RDI, reinterpret_cast<uintptr_t>(edge)));

  // Call into the direct edge entry code, which might disable interrupts, and
  // will transfer control to a private stack.
  ENC(CALL_NEAR_RELBRd(&ni, DirectExitFunction()));

  // Restore the stolen `RDI`.
  ENC(POP_GPRv_51(&ni, XED_REG_RDI));

  // Restore back to the native stack.
  if (REDZONE_SIZE_BYTES && !target_stack_valid) {
    ENC(UNSHIFT_REDZONE(&ni));
  }

  // Jump back to the edge entrypoint. The `entry_target_pc` should now be
  // resolved.
  ENC(JMP_RELBRd(&ni, edge->edge_code_pc));

  GRANARY_ASSERT(arch::DIRECT_EDGE_CODE_SIZE_BYTES >= (pc - start_pc));
}

// Generates the indirect edge entry code for getting onto a Granary private
// stack, disabling interrupts, etc.
void GenerateIndirectEdgeEntryCode(CachePC pc) {
  Instruction ni;
  InstructionEncoder stage_enc(InstructionEncodeKind::STAGED);
  InstructionEncoder commit_enc(InstructionEncodeKind::COMMIT);
  GRANARY_IF_DEBUG( const auto start_pc = pc; )

  // Save the flags and potentially disable interrupts.
  ENC(PUSHFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

  if (GRANARY_IF_USER_ELSE(false, true)) {
    // Disable interrupts and swap onto Granary's private stack.
    ENC(CLI(&ni));
    ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(os::SLOT_PRIVATE_STACK), XED_REG_RSP));
  }

  // Transfer control to a generic Granary direct edge entrypoint. Try to be
  // smart about encoding the target.
  ENC(CALL_NEAR_RELBRd(&ni, &granary_arch_enter_indirect_edge));

  if (GRANARY_IF_USER_ELSE(false, true)) {
    // Swap back to the native stack.
    ENC(XCHG_MEMv_GPRv(&ni, SlotMemOp(os::SLOT_PRIVATE_STACK), XED_REG_RSP));
  }

  // Restore the flags, and potentially re-enable interrupts. After this
  // instruction, it is reasonably likely that we will hit an interrupt.
  ENC(POPFQ(&ni); ni.effective_operand_width = arch::GPR_WIDTH_BITS; );

  // Return back into the in-edge code.
  ENC(RET_NEAR(&ni); ni.effective_operand_width = arch::ADDRESS_WIDTH_BITS; );

  GRANARY_ASSERT(arch::INDIRECT_EDGE_ENTRY_CODE_SIZE_BYTES >= (pc - start_pc));
}

// Update the attribute info of an indirect edge fragment.
static void UpdateIndirectEdgeFrag(CodeFragment *edge_frag,
                                   CodeFragment *pred_frag,
                                   BlockMetaData *dest_block_meta) {
  edge_frag->block_meta = dest_block_meta;

  // Prevent this fragment from being reaped by `RemoveUselessFrags` in
  // `3_partition_fragments.cc`.
  edge_frag->attr.has_native_instrs = true;

  // Mark this code as instrumentation code as we do modify the flags.
  edge_frag->kind = kFragmentKindInst;

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
//            about_to_exit
//                 |
//            exit_to_block
//
CodeFragment *GenerateIndirectEdgeCode(FragmentList *frags, IndirectEdge *edge,
                                       ControlFlowInstruction *cfi,
                                       CodeFragment *pred_frag,
                                       BlockMetaData *dest_block_meta) {
  GRANARY_ASSERT(!cfi->IsFunctionReturn());
  cfi->instruction.DontEncode();

  auto in_edge = new CodeFragment;
  auto go_to_granary = new CodeFragment;
  auto compare_target = new CodeFragment;
  auto exit_to_block = new ExitFragment;

  // Set the code cache types.
  go_to_granary->cache = kCodeCacheKindEdge;
  compare_target->cache = kCodeCacheKindEdge;
  exit_to_block->cache = kCodeCacheKindEdge;

  // Set up the edges. Some of these are "sort of" lies, in the sense that
  // we will often use the combination of a `branch_instr` and
  // `FRAG_SUCC_BRANCH` to trick `10_add_connecting_jumps.cc` to put the
  // fragments in the desired order.
  in_edge->successors[kFragSuccBranch] = go_to_granary;
  go_to_granary->successors[kFragSuccFallThrough] = compare_target;
  compare_target->successors[kFragSuccFallThrough] = exit_to_block;
  compare_target->successors[kFragSuccBranch] = go_to_granary;
  exit_to_block->block_meta = dest_block_meta;

  // Add the fragments, and set some of their attributes.
  frags->Append(in_edge);
  frags->Append(go_to_granary);
  frags->Append(compare_target);
  frags->Append(exit_to_block);

  UpdateIndirectEdgeFrag(in_edge, pred_frag, dest_block_meta);
  UpdateIndirectEdgeFrag(go_to_granary, pred_frag, dest_block_meta);
  UpdateIndirectEdgeFrag(compare_target, pred_frag, dest_block_meta);

  Instruction ni;

  // Get the target of the CFI into a register.
  const auto &target_op(cfi->instruction.ops[0]);
  GRANARY_ASSERT(target_op.IsRegister());  // Enforced by `1_mangle.cc`.

  // --------------------- in_edge --------------------------------

  in_edge->instrs.Append(new AnnotationInstruction(
      kAnnotSaveRegister, REG_RSI));

  if (cfi->IsFunctionCall()) {
    APP(in_edge, CALL_NEAR_MEMv(&ni, &(edge->out_edge_pc));
                   ni.is_sticky = true; );
  } else if (cfi->IsUnconditionalJump()) {
    APP(in_edge, JMP_MEMv(&ni, &(edge->out_edge_pc));
                   ni.is_sticky = true; );
  } else {
    GRANARY_ASSERT(false);
  }

  // We put this in so that `10_add_connecting_jumps` is tricked into thinking
  // that it doesn't need to add in a fall-through / branch instruction.
  in_edge->branch_instr = DynamicCast<NativeInstruction *>(
      in_edge->instrs.Last());

  // --------------------- go_to_granary --------------------------------

  // For the fall-through; want to make sure no weird register allocation
  // stuff gets in the way.
  auto miss_addr = new AnnotationInstruction(kAnnotUpdateAddressWhenEncoded,
                                             &(edge->out_edge_pc));
  go_to_granary->instrs.Append(miss_addr);

  // Store the branch target into `RSI` and theaddress of the `IndirectEdge`
  // data structure in `RDI`. Jump to `edge->in_edge_pc`, which is initialized
  // to be the indirect edge entrypoint edge code.
  auto cfi_target = target_op.reg;
  GRANARY_ASSERT(cfi_target.IsVirtual());

  go_to_granary->instrs.Append(new AnnotationInstruction(
      kAnnotSaveRegister, REG_RDI));
  APP(go_to_granary, MOV_GPRv_IMMv(&ni, XED_REG_RDI, edge));
  APP(go_to_granary, MOV_GPRv_GPRv_89(&ni, XED_REG_RSI, cfi_target); );

  // Call into the indirect edge entry code.
  go_to_granary->instrs.Append(
      new AnnotationInstruction(kAnnotCondLeaveNativeStack));
  APP(go_to_granary, CALL_NEAR_RELBRd(&ni, IndirectExitFunction());
                     ni.is_sticky = true;
                     ni.is_stack_blind = true; );
  go_to_granary->instrs.Append(
      new AnnotationInstruction(kAnnotCondEnterNativeStack));

  go_to_granary->instrs.Append(new AnnotationInstruction(
      kAnnotRestoreRegister, REG_RDI));
  APP(go_to_granary, JMP_MEMv(&ni, &(edge->out_edge_pc));
                     ni.is_sticky = true; );

  auto begin_template = new AnnotationInstruction(
      kAnnotUpdateAddressWhenEncoded, &(edge->out_edge_template));
  go_to_granary->instrs.Append(begin_template);

  // --------------------- compare_target --------------------------------

  compare_target->attr.reads_flags = true;
  compare_target->attr.modifies_flags = true;

  // Gets updated later by:
  //    1)  Moving the target of the control-flow instruction into `RSI`
  //        (first instruction).
  //    2)  Jumping directly to the targeted basic block (last instruction).
  APP(compare_target, MOV_GPRv_IMMv(&ni, XED_REG_RSI, 0UL);
                      ni.dont_encode = true; );
  APP(compare_target, CMP_GPRv_GPRv_39(&ni, XED_REG_RSI, cfi_target));
  APP(compare_target, JNZ_RELBRd(&ni, nullptr));
  compare_target->branch_instr = DynamicCast<NativeInstruction *>(
      compare_target->instrs.Last());

  compare_target->instrs.Append(new AnnotationInstruction(
      kAnnotRestoreRegister, REG_RSI));

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
  Instruction ni;
  Instruction mov;

  auto first_frag = frags->First();
  auto frag = new Fragment;
  frags->Prepend(frag);
  frag->next = first_frag;
  frag->cache = first_frag->cache;
  frag->successors[kFragSuccFallThrough] = first_frag;

  GRANARY_IF_DEBUG( auto added_mov_addr = false; )
  for (auto pc = edge->out_edge_template;
       InstructionDecoder::DecodeNext(&ni, &pc); ) {

    // Look for the `CMP` that compares the address with the target.
    if (XED_ICLASS_CMP == ni.iclass) {
      MOV_GPRv_IMMv(&mov, XED_REG_RSI, reinterpret_cast<intptr_t>(app_pc));
      frag->instrs.Append(new NativeInstruction(&mov));
      GRANARY_IF_DEBUG( added_mov_addr = true; )

    // Re-target the failure case
    } else if (XED_ICLASS_JNZ == ni.iclass) {
      ni.SetBranchTarget(edge->out_edge_pc);  // Miss! Jump to fall-back.
    }
    APP(frag);
  }
}

// Patch a direct edge.
//
// Note: This function has an architecture-specific implementation.
bool TryAtomicPatchEdge(DirectEdge *edge) {
  Instruction ni;
  InstructionEncoder stage_enc(InstructionEncodeKind::STAGED);
  InstructionEncoder commit_enc(InstructionEncodeKind::COMMIT_ATOMIC);

  // If we fail to decode the instruction then don't patch it.
  if (!InstructionDecoder::Decode(&ni, edge->patch_instruction_pc)) {
    return false;
  }
  const auto decoded_length = ni.decoded_length;

  // If the decoded length is greater than 8 bytes then don't patch it.
  if (8 < decoded_length) return false;

  // If the instruction crosses two cache lines then don't patch it.
  auto decode_addr = reinterpret_cast<uintptr_t>(edge->patch_instruction_pc);
  auto start_cl = decode_addr / CACHE_LINE_SIZE_BYTES;
  auto end_cl = (decode_addr + decoded_length - 1) / CACHE_LINE_SIZE_BYTES;
  if (start_cl != end_cl) return false;

  ni.SetBranchTarget(edge->entry_target_pc);
  stage_enc.Encode(&ni, edge->patch_instruction_pc);

  // If the instruction length changes then don't patch it.
  if (ni.encoded_length != decoded_length) return false;

  CodeCacheTransaction transaction;
  commit_enc.Encode(&ni, edge->patch_instruction_pc);
  return true;
}

}  // namespace arch
}  // namespace granary
