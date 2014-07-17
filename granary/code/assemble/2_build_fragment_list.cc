/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"

#include "granary/code/edge.h"
#include "granary/code/fragment.h"
#include "granary/code/assemble/2_build_fragment_list.h"

#include "granary/cache.h"
#include "granary/context.h"
#include "granary/util.h"

namespace granary {
namespace arch {

// The high-level goal of this stage of assembly is to take input basic blocks
// from a local-control-flow graph and turn them into "true" basic blocks (with
// some added restrictions one when these true blocks end) and form a control-
// flow graph.
//
// At decode time, the local control-flow graph is formed of "true" basic
// blocks. However, instrumentation tools might inject abitrary control-flow
// into basic blocks (e.g. via inline assembly). By the time we get around to
// wanting to convert instrumented blocks into machine code, we hit a wall
// where we can't assume that control flows linearly through the instructions
// of a `DecodedBasicBlock`, and this really complicates virtual register
// allocation (which is a pre-requisite to encoding).
//
// Therefore, it's necessary to "re-split up" `DecodedBasicBlocks` into actual
// basic blocks. However, we go further than the typical definition of a basic
// block, hence the name `Fragment`.
//
// A `Fragment` is a maximal sequence of instructions ending in an instruction
// that:
//      1)  Branches somewhere else (a control-flow instructions).
//      2)  Alters the stack pointer. This extra condition is used during
//          stage 4, to partition / color fragments. The key idea here is that
//          in kernel space, we can use the stack for allocating virtual
//          registers if the stack is "safe" (i.e. behaves like a C-style
//          call stack). An example of an "unsafe" stack is a user space stack.
//      3)  Is or is not an application / native instruction. That is, fragments
//          contain either all application or all instrumentation instructions.
//          This makes flags usage analysis, saving, and restoring easier
//          because then we can reason about the problem at the granularity of
//          fragments, and employ data flow frameworks to tackle the problem.
//      4)  Is a label instruction. Label instructions are assumed to be
//          targeted by local branch instructions, and so we eagerly split
//          fragments at label instructions based on this assumption.

// Does this instruction hint that the fragment should be split before the next
// modification of the flags?
//
// Note: This function has an architecture-specific implementation.
bool InstructionHintsAtFlagSplit(const NativeInstruction *instr);

// Returns true if this instruction can change the interrupt enabled state on
// this CPU.
//
// Note: This function has an architecture-specific implementation.
extern bool ChangesInterruptDeliveryState(const NativeInstruction *instr);

// Generates some indirect edge code that is used to look up the target of an
// indirect jump.
//
// Note: This function has an architecture-specific implementation.
void GenerateIndirectEdgeCode(IndirectEdge *edge,
                              ControlFlowInstruction *cfi,
                              CodeFragment *in_edge,
                              CodeFragment *out_edge_miss,
                              CodeFragment *out_edge_hit,
                              ExitFragment *out_edge_exit);

}  // namespace arch
namespace {

struct FragmentListBuilder {
  ContextInterface *context;
  LocalControlFlowGraph *cfg;
  FragmentList *frags;

  void Append(Fragment *frag) {
    frags->Append(frag);
  }
};

// Make a new code fragment.
static CodeFragment *MakeFragment(FragmentListBuilder *frags) {
  auto frag = new CodeFragment;
  frags->Append(frag);
  return frag;
}

// Make a fragment for a native basic block.
static ExitFragment *MakeNativeFragment(FragmentListBuilder *frags,
                                        NativeBasicBlock *block) {
  auto frag = new ExitFragment(FRAG_EXIT_NATIVE);
  frags->Append(frag);
  frag->encoded_pc = UnsafeCast<CachePC>(block->StartAppPC());
  return frag;
}

// Make a block head fragment for a cached basic block. This means importing
// its register schedule as hard constraints.
static ExitFragment *MakeCachedFragment(FragmentListBuilder *frags,
                                        CachedBasicBlock *block) {
  auto frag = new ExitFragment(FRAG_EXIT_EXISTING_BLOCK);
  frags->Append(frag);
  frag->block_meta = block->UnsafeMetaData();
  frag->encoded_pc = block->StartCachePC();
  return frag;
}

// Create a new fragment starting at a label.
static CodeFragment *MakeEmptyLabelFragment(FragmentListBuilder *frags,
                                            DecodedBasicBlock *block,
                                            LabelInstruction *label) {
  auto frag = MakeFragment(frags);
  frag->attr.block_meta = block->UnsafeMetaData();
  frag->instrs.Append(label->UnsafeUnlink().release());
  SetMetaData(label, frag);
  return frag;
}

// Returns true if this instruction would convert the current fragmnet into
// application code.
static bool InstrMakesFragmentIntoAppCode(NativeInstruction *instr) {
  if (!instr->IsAppInstruction()) return false;

  // Minor optimization: we want to arrange for potential flag save/restores to
  // span from a fragment all the way into edge code, instead of potentially
  // have some before the indirect CTI and some after.
  if (instr->IsFunctionCall() || instr->IsUnconditionalJump()) {
    if (instr->HasIndirectTarget()) {
      return false;
    }

  // Only do the optimization for specialized function returns; unspecialized
  // returns use an identity translation and therefore do not use edge code.
  } else if (instr->IsFunctionReturn()) {
    auto target_block = DynamicCast<ControlFlowInstruction *>(instr)
        ->TargetBlock();
    auto ret_block = DynamicCast<ReturnBasicBlock *>(target_block);
    if (ret_block->UnsafeMetaData()) {
      return false;
    }
  }

  return instr->instruction.WritesToStackPointer() ||
         instr->ReadsConditionCodes() ||
         instr->WritesConditionCodes();
}

// Append an instruction to a fragment. This also does minor flag usage analysis
// of the instruction to figure out if the fragment should be treated as an
// application fragment or an instrumentation code fragment.
static CodeFragment *Append(FragmentListBuilder *frags, DecodedBasicBlock *block,
                            CodeFragment *frag, Instruction *instr) {
  instr->UnsafeUnlink().release();
  auto ninstr = DynamicCast<NativeInstruction *>(instr);
  if (!ninstr) {
    frag->instrs.Append(instr);
    return frag;
  }

  const auto modifies_flags = ninstr->WritesConditionCodes();
  auto hints_at_split = false;
  auto makes_frag_into_app = InstrMakesFragmentIntoAppCode(ninstr);

  if (frag->attr.is_app_code) {
    // Non-application instruction being added to an application fragment, need
    // to split.
    if (modifies_flags && !makes_frag_into_app) {
      goto split;
    }
  } else {
    if (makes_frag_into_app) {

      // Adding an application instruction to a non-app fragment that already
      // has (non-app) instructions that modify the flags.
      if (frag->attr.modifies_flags) goto split;

    // Instruction modifies the flags, and some prior instruction in the
    // fragment has hinted that the fragment should be split instead of
    // allowing flag-modifying instructions to be appended.
    } else if (frag->attr.has_flag_split_hint && modifies_flags) {

      // Early conversion of an instrumentation fragment into an application
      // fragment.
      if (!frag->attr.modifies_flags) {
        frag->attr.is_app_code = true;
      }
      goto split;

    // Instruction doesn't modify the flags or make the fragment into app code,
    // but might make us want to split this fragment (and ideally convert it
    // into an app fragment) before an instrumentation instruction next modifies
    // the flags.
    } else {
      hints_at_split = arch::InstructionHintsAtFlagSplit(ninstr);

      // Early conversion into app code for app instructions that hint at
      // flag splits.
      const auto is_app_instr = ninstr->IsAppInstruction();
      if (hints_at_split && is_app_instr) {
        makes_frag_into_app = true;
        hints_at_split = false;
        if (frag->attr.modifies_flags) goto split;
      }
    }
  }
  goto append;

  split: {
    auto succ = MakeEmptyLabelFragment(frags, block, new LabelInstruction);
    frag->successors[FRAG_SUCC_FALL_THROUGH] = succ;
    frag = succ;
  }
  append: {
    frag->attr.has_native_instrs = true;
    if (makes_frag_into_app) frag->attr.is_app_code = true;
    if (modifies_flags) frag->attr.modifies_flags = true;
    if (hints_at_split) frag->attr.has_flag_split_hint = true;
    frag->instrs.Append(instr);
  }
  return frag;
}

// Extend a fragment with the instructions from a particular basic block.
// This might end up generating many more fragments.
static void ExtendFragment(FragmentListBuilder *frags, CodeFragment *frag,
                           DecodedBasicBlock *block, Instruction *instr);

// Get or make the fragment starting at a label.
static CodeFragment *GetOrMakeLabelFragment(FragmentListBuilder *frags,
                                            DecodedBasicBlock *block,
                                            LabelInstruction *label) {
  CodeFragment *frag = GetMetaData<CodeFragment *>(label);
  if (!frag) {
    auto next = label->Next();
    frag = MakeEmptyLabelFragment(frags, block, label);
    ExtendFragment(frags, frag, block, next);
  }
  return frag;
}

// Split a fragment into two at a label instruction `instr`. If the label
// is already associated with a `Fragment` instance then set that fragment
// as the fall-through of our current fragment. If new `Fragment` instance
// is associated with the label, then create one, add the association, and
// add the instructions following the label into the new fragment.
static void SplitFragmentAtLabel(FragmentListBuilder *frags, CodeFragment *frag,
                                 DecodedBasicBlock *block, Instruction *instr) {
  auto label_fragment = GetMetaData<CodeFragment *>(instr);
  if (label_fragment) {  // Already processed this fragment.
    frag->successors[0] = label_fragment;
  } else if (auto label = DynamicCast<LabelInstruction *>(instr)) {
    auto block_meta = block->UnsafeMetaData();
    auto next = label->Next();

    // Create a new successor fragment.
    if (frag->attr.has_native_instrs ||
        label->data ||  // If non-zero then it's likely targeted by a branch.
        (frag->attr.is_block_head && frag->attr.block_meta != block_meta)) {
      auto succ = MakeEmptyLabelFragment(frags, block, label);
      frag->successors[0] = succ;
      frag = succ;

    // Extend the current fragment in-place.
    } else {
      GRANARY_ASSERT(!frag->attr.block_meta ||
                     (frag->attr.block_meta == block_meta));
      frag->attr.block_meta = block_meta;
      SetMetaData(label, frag);
      frag->instrs.Append(label->UnsafeUnlink().release());
    }
    ExtendFragment(frags, frag, block, next);

  } else {
    GRANARY_ASSERT(false);
  }
}

// Split a fragment into two at a local branch instruction. First get or
// create the fragment associated with the branch target. Then create a
// fragment for the fall-through of the branch, and include remaining
// instructions from the block into that fragment.
static void SplitFragmentAtBranch(FragmentListBuilder *frags, CodeFragment *frag,
                                  DecodedBasicBlock *block,
                                  BranchInstruction *branch) {
  auto label = branch->TargetInstruction();
  auto next = branch->Next();

  // Conditional jump, therefore we have to create two successor fragments.
  if (branch->IsConditionalJump()) {
    frag = Append(frags, block, frag, branch);
    auto succ = MakeEmptyLabelFragment(frags, block, new LabelInstruction);
    frag->successors[FRAG_SUCC_FALL_THROUGH] = succ;
    ExtendFragment(frags, succ, block, next);
    frag->successors[FRAG_SUCC_BRANCH] = GetOrMakeLabelFragment(
        frags, block, label);
    GRANARY_ASSERT(nullptr == frag->branch_instr);
    frag->branch_instr = branch;

  // Unconditional jump, and the current instruction is either a block head or
  // has instructions in it, so we can't convert this fragment into the target
  // fragment.
  } else if (frag->attr.has_native_instrs ||
             GetMetaData<CodeFragment *>(label)) {
    frag->successors[FRAG_SUCC_FALL_THROUGH] = GetOrMakeLabelFragment(
        frags, block, label);

  // This fragment has no "useful" instructions in it, it's not a block head,
  // and we've got an unconditional jump. Convert the current fragment into the
  // target fragment.
  } else {
    SetMetaData<CodeFragment *>(label, frag);
    frag->attr.block_meta = block->UnsafeMetaData();
    frag = Append(frags, block, frag, label);
    ExtendFragment(frags, frag, block, next);
  }
}

// Get the list of fragments associated with a basic block.
static CodeFragment *FragmentForBlock(FragmentListBuilder *frags,
                                      DecodedBasicBlock *block);

// Generates some edge code for a direct control-flow transfer between two
// basic block.
Fragment *MakeDirectEdgeFragment(FragmentListBuilder *frags,
                                 BlockMetaData *dest_block_meta) {
  auto edge_frag = new ExitFragment(FRAG_EXIT_FUTURE_BLOCK_DIRECT);
  auto edge = frags->context->AllocateDirectEdge(dest_block_meta);
  edge_frag->encoded_pc = edge->edge_code;
  edge_frag->block_meta = dest_block_meta;
  edge_frag->edge.kind = EDGE_KIND_DIRECT;
  edge_frag->edge.direct = edge;
  frags->Append(edge_frag);
  return edge_frag;
}

// Update the attribute info of an indirect edge fragment.
static void UpdateIndirectEdgeFrag(CodeFragment *edge_frag,
                                   CodeFragment *pred_frag,
                                   BlockMetaData *dest_block_meta) {
  edge_frag->stack.is_valid = pred_frag->stack.is_valid;
  edge_frag->stack.is_checked = pred_frag->stack.is_checked;
  edge_frag->attr.block_meta = dest_block_meta;

  // Prevent this fragment from being reaped by `RemoveUselessFrags` in
  // `3_partition_fragments.cc`.
  edge_frag->attr.has_native_instrs = true;

  // Make sure that the edge code shares the same partition as the predecessor
  // so that virtual registers can be spread across both.
  edge_frag->attr.can_add_to_partition = true;
  edge_frag->partition.Union(edge_frag, pred_frag);
}

// Make an edge fragment and an exit fragment for some future block.
static Fragment *MakeIndirectEdgeFragment(FragmentListBuilder *frags,
                                          CodeFragment *pred_frag,
                                          BlockMetaData *dest_block_meta,
                                          ControlFlowInstruction *cfi) {
  auto in_edge_frag = new CodeFragment;
  in_edge_frag->attr.is_in_edge_code = true;
  UpdateIndirectEdgeFrag(in_edge_frag, pred_frag, dest_block_meta);

  auto out_edge_frag_miss = new CodeFragment;
  out_edge_frag_miss->attr.is_in_edge_code = false;
  UpdateIndirectEdgeFrag(out_edge_frag_miss, in_edge_frag, dest_block_meta);

  auto out_edge_frag_hit = new CodeFragment;
  out_edge_frag_hit->attr.is_in_edge_code = false;
  UpdateIndirectEdgeFrag(out_edge_frag_hit, in_edge_frag, dest_block_meta);

  auto exit_frag = new ExitFragment(FRAG_EXIT_FUTURE_BLOCK_INDIRECT);
  exit_frag->edge.kind = EDGE_KIND_INDIRECT;
  exit_frag->block_meta = dest_block_meta;

  auto edge = frags->context->AllocateIndirectEdge(dest_block_meta);

  arch::GenerateIndirectEdgeCode(edge, cfi, in_edge_frag, out_edge_frag_miss,
                                 out_edge_frag_hit, exit_frag);

  // Note: `branch_instr` of `in_edge_frag` is initialized by
  //       `arch::GenerateIndirectEdgeCode`.
  //
  // Note: `branch_instr` of `out_edge_frag_hit` may or may not be initialized
  //       by `arch::GenerateIndirectEdgeCode`, depending on what code is
  //       generated.

  in_edge_frag->successors[FRAG_SUCC_FALL_THROUGH] = out_edge_frag_miss;
  in_edge_frag->successors[FRAG_SUCC_BRANCH] = out_edge_frag_hit;

  out_edge_frag_hit->successors[FRAG_SUCC_FALL_THROUGH] = exit_frag;
  out_edge_frag_hit->successors[FRAG_SUCC_BRANCH] = out_edge_frag_miss;

  out_edge_frag_miss->successors[FRAG_SUCC_BRANCH] = out_edge_frag_hit;

  frags->Append(in_edge_frag);
  frags->Append(out_edge_frag_miss);
  frags->Append(out_edge_frag_hit);
  frags->Append(exit_frag);

  return in_edge_frag;
}

static ExitFragment *MakeExitFragment(FragmentListBuilder *frags) {
  auto exit_frag = new ExitFragment(FRAG_EXIT_NATIVE);
  frags->Append(exit_frag);
  return exit_frag;
}

// Return the fragment for a block that is targeted by a control-flow
// instruction.
static Fragment *FragmentForTargetBlock(FragmentListBuilder *frags,
                                        CodeFragment *pred_frag,
                                        BasicBlock *target_block,
                                        ControlFlowInstruction *cfi) {
  // Function/interrupt/system return. In these cases, we can't be sure (at
  // instrumentation time) that execution returns to the code cache.
  //
  // OR:
  //
  // Direct call/jump to native; interrupt call, system call. All regs
  // must be homed on exit of this block lets things really screw up.
  if (auto native_block = DynamicCast<NativeBasicBlock *>(target_block)) {
    return MakeNativeFragment(frags, native_block);
  }

  // Direct call/jump/conditional jump to a decoded block.
  if (auto decoded_block = DynamicCast<DecodedBasicBlock *>(target_block)) {
    return FragmentForBlock(frags, decoded_block);
  }

  // Direct call/jump/conditional jump to a cached block.
  if (auto cached_block = DynamicCast<CachedBasicBlock *>(target_block)) {
    return MakeCachedFragment(frags, cached_block);
  }

  // Direct jump / conditional jump to an unresolved block, need to add in
  // some edge code.
  if (auto direct_block = DynamicCast<DirectBasicBlock *>(target_block)) {
    // TODO(pag): If the number of predecessors of this block is >= 2 then it
    //            would be nice if they could share the same edge code via some
    //            intermediate fragment with the patchable jump.
    return MakeDirectEdgeFragment(frags, direct_block->MetaData());
  }

  // Indirect call/jump, or direct call/jump/conditional jump
  // to a future block.
  if (IsA<ReturnBasicBlock *>(target_block) ||
      IsA<IndirectBasicBlock *>(target_block)) {
    auto inst_block = DynamicCast<InstrumentedBasicBlock *>(target_block);
    if (auto block_meta = inst_block->UnsafeMetaData()) {
      return MakeIndirectEdgeFragment(frags, pred_frag, block_meta, cfi);
    } else {
      return MakeExitFragment(frags);
    }
  }

  GRANARY_ASSERT(false);
  return nullptr;
}

// Append a CFI to a fragment, and potentially make a new fragment for the CFI.
static CodeFragment *AppendCFI(FragmentListBuilder *frags, CodeFragment *frag,
                               DecodedBasicBlock *block,
                               Fragment *target_frag,
                               ControlFlowInstruction *cfi) {
  const auto makes_stack_valid = cfi->IsFunctionCall() ||
                                 cfi->IsFunctionReturn() ||
                                 cfi->IsInterruptReturn();

  // Does this CFI target some edge code (e.g. indirect in-edge code, or direct
  // edge code?). We determine it here instead of by modifying `pred_frag` in
  // `MakeDirectEdgeFragment` and `MakeIndirectEdgeFragment` because we might
  // need to isolate `cfi` in its own fragment, and so `pred_frag` wouldn't
  // match anymore.
  auto targets_edge_code = false;
  auto targets_direct_edge_code = false;
  auto can_add_to_partition = false;
  auto force_add_to_frag = !frag->attr.has_native_instrs;

  if (cfi->HasIndirectTarget()) {
    targets_edge_code = IsA<CodeFragment *>(target_frag);
    can_add_to_partition = targets_edge_code;
  } else if (auto exit_target_frag = DynamicCast<ExitFragment *>(target_frag)) {
    targets_edge_code = EDGE_KIND_INVALID != exit_target_frag->edge.kind;
    targets_direct_edge_code = EDGE_KIND_DIRECT == exit_target_frag->edge.kind;
  }

  // We need to add a new fragment for this CFI.
  auto frag_with_cfi = frag;
  if (!can_add_to_partition && !force_add_to_frag) {
    auto label = new LabelInstruction;
    auto succ = MakeEmptyLabelFragment(frags, block, label);
    frag->successors[FRAG_SUCC_FALL_THROUGH] = succ;
    frag_with_cfi = succ;
  }

  frag_with_cfi = Append(frags, block, frag_with_cfi, cfi);

  // This CFI is something like a function call / return, i.e. it makes the
  // stack pointer appear to point to a C-style call stack.
  if (makes_stack_valid) {
    frag_with_cfi->stack.is_valid = true;
    frag_with_cfi->stack.is_checked = true;
  }
  frag_with_cfi->attr.can_add_to_partition = can_add_to_partition;
  if (targets_edge_code) {
    frag_with_cfi->attr.branches_to_edge_code = true;
  }
  return frag_with_cfi;
}

// Split a fragment at a non-local control-flow instruction.
static void SplitFragmentAtCFI(FragmentListBuilder *frags, CodeFragment *frag,
                               DecodedBasicBlock *block,
                               ControlFlowInstruction *cfi) {
  auto next = cfi->Next();
  auto target_block = cfi->TargetBlock();
  auto target_frag = FragmentForTargetBlock(frags, frag, target_block, cfi);

  // Direct jump to another block.
  if (cfi->IsUnconditionalJump() && !cfi->HasIndirectTarget() &&
      IsA<CodeFragment *>(target_frag)) {
    frag->successors[FRAG_SUCC_FALL_THROUGH] = target_frag;
    return;  // No fall-through.
  }

  // One of:
  //    1) Direct jump to edge code.
  //    2) Conditional jump.
  //    3) Function/interrupt/system call/return.
  frag = AppendCFI(frags, frag, block, target_frag, cfi);
  GRANARY_ASSERT(nullptr == frag->branch_instr);
  frag->successors[FRAG_SUCC_BRANCH] = target_frag;
  frag->branch_instr = cfi;

  if (cfi->IsFunctionReturn() || cfi->IsInterruptReturn() ||
      cfi->IsSystemReturn() || cfi->IsUnconditionalJump()) {
    return;  // No fall-through.
  }

  auto label = new LabelInstruction;
  auto succ = MakeEmptyLabelFragment(frags, block, label);
  frag->successors[FRAG_SUCC_FALL_THROUGH] = succ;
  succ->attr.can_add_to_partition = false;
  succ->attr.block_meta = frag->attr.block_meta;
  ExtendFragment(frags, succ, block, next);
}

// Split a fragment at a place where the validness of the stack pointer changes
// from defined to undefined, or undefined to defined.
static void SplitFragmentAtStackChange(FragmentListBuilder *frags, CodeFragment *frag,
                                       DecodedBasicBlock *block,
                                       Instruction *instr,
                                       bool stack_is_valid) {
  auto label = new LabelInstruction;
  auto succ = MakeEmptyLabelFragment(frags, block, label);
  frag->successors[0] = succ;
  succ->stack.is_checked = true;
  succ->stack.is_valid = stack_is_valid;
  ExtendFragment(frags, succ, block, instr);
}

// Split a fragment at a point where the instructions in the block change
// from instrumentation-added -> app, or app -> instrumentation added.
static void SplitFragment(FragmentListBuilder *frags, CodeFragment *frag,
                          DecodedBasicBlock *block, Instruction *next) {
  auto label = new LabelInstruction;
  auto succ = MakeEmptyLabelFragment(frags, block, label);
  frag->successors[0] = succ;
  ExtendFragment(frags, succ, block, next);
}

// Split a fragment at an instruction that changes the interrupt state.
static void SplitFragmentAtInterruptChange(FragmentListBuilder *frags,
                                           CodeFragment *frag,
                                           DecodedBasicBlock *block,
                                           Instruction *instr) {
  auto next = instr->Next();
  if (frag->attr.has_native_instrs) {
    auto label = new LabelInstruction;
    auto succ = MakeEmptyLabelFragment(frags, block, label);
    frag->successors[0] = succ;
    frag = succ;
  }
  frag->attr.can_add_to_partition = false;
  frag = Append(frags, block, frag, instr);
  SplitFragment(frags, frag, block, next);
}

// Extend a fragment with the instructions from a particular basic block.
// This might end up generating many more fragments.
static void ExtendFragment(FragmentListBuilder *frags, CodeFragment *frag,
                           DecodedBasicBlock *block, Instruction *instr) {

  const auto last_instr = block->LastInstruction();
  for (; instr != last_instr; ) {

    // Treat every label as beginning a new fragment.
    if (IsA<LabelInstruction *>(instr)) {
      return SplitFragmentAtLabel(frags, frag, block, instr);
    }

    // Split instructions into fragments such that fragments contain either
    // all native instructions, or all instrumentation instructions, but not
    // both. This splitting is used in a later stage to allow us to reason
    // about saving/restoring flags state between two native instructions
    // that are separated by instrumentation instructions.
    //
    // One exception to this rule is that if the current instruction doesn't
    // affect the flags, regardless of if it's native/instrumented, it goes
    // into whatever the previous section of code is (app or inst).
    if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
      if (arch::ChangesInterruptDeliveryState(ninstr)) {
        return SplitFragmentAtInterruptChange(frags, frag, block, instr);
      } else if (frag->attr.is_app_code) {
        if (!ninstr->IsAppInstruction() && ninstr->WritesConditionCodes()) {
          return SplitFragment(frags, frag, block, instr);
        }
      } else if (frag->attr.modifies_flags &&  // Must have at least 1 instr.
                 InstrMakesFragmentIntoAppCode(ninstr)) {
        return SplitFragment(frags, frag, block, instr);
      }
    }

    // Found a local branch; add in the fall-through and/or the branch
    // target.
    if (auto branch = DynamicCast<BranchInstruction *>(instr)) {
      return SplitFragmentAtBranch(frags, frag, block, branch);

    // Found a non-local branch to a basic block.
    } else if (auto cfi = DynamicCast<ControlFlowInstruction *>(instr)) {
      return SplitFragmentAtCFI(frags, frag, block, cfi);

    // Ignore annotation instructions, but use them to guide the fragment
    // splitting w.r.t stack definedness. The stack definedness annotations
    // come from early mangling of instructions. It's important that they
    // come from there as they will be inserted before any virtual registers
    // created for use by a particular stack pointer changing instruction.
    } else if (auto annot = DynamicCast<AnnotationInstruction *>(instr)) {
      auto next = instr->Next();
      if (IA_VALID_STACK == annot->annotation) {
        if (frag->stack.is_checked && !frag->stack.is_valid &&
            frag->attr.has_native_instrs) {
          return SplitFragmentAtStackChange(frags, frag, block, next, true);
        } else {
          frag->stack.is_checked = true;
          frag->stack.is_valid = true;
        }
      } else if (IA_UNDEFINED_STACK == annot->annotation) {
        if ((frag->stack.is_checked && frag->stack.is_valid) ||
            frag->attr.has_native_instrs) {
          return SplitFragmentAtStackChange(frags, frag, block, next, false);
        } else {
          frag->stack.is_checked = true;
          frag->stack.is_valid = false;
        }
      // This annotation is somewhat more subtle than the above two. The idea
      // is that when we do the stack analysis and fragment partitioning in
      // `3_partition_fragments.cc`, we want to be aggressive about stack
      // validity. So, for example, if we see something like:
      //          MOV RSP, [X]
      //          MOV Y, [Z]
      //          POP [Y]
      // Then we'll split that into two fragments:
      //          MOV RSP, [X]
      //          ------------
      //          MOV Y, [Z]
      //          POP [Y]
      // Where the `MOV Y, [Z]` is grouped with the `POP` and so isn't penalized
      // by the stack undefinedness of the `MOV RSP, [X]`.
      } else if (IA_UNKNOWN_STACK_ABOVE == annot->annotation) {
        GRANARY_ASSERT(!frag->stack.is_checked || !frag->stack.is_valid);
        frag->stack.is_checked = true;
        frag->stack.is_valid = false;
        return SplitFragment(frags, frag, block, next);

      // Here we've got something like:
      //          PUSH RBP
      //          <IA_UNKNOWN_STACK_BELOW>
      //          MOV RBP, RSP
      //          MOV [RBP - 8], RDI   <--- accesses into redzone, below RSP
      // So `early_mangle.cc` will have added an annotation before the copy of
      // `RSP`.
      //
      // Note: This annotation is only generated if `REDZONE_SIZE_BYTES > 0`.
      } else if (IA_UNKNOWN_STACK_BELOW == annot->annotation) {
        frag->stack.disallow_forward_propagation = true;
        return SplitFragment(frags, frag, block, next);

      // Special case related to indirect call mangling. Indirect calls might
      // be mangled into pushes of a return address, followed by indirect
      // jumps.
      } else if (IA_RETURN_ADDRESS == annot->annotation) {
        frag = Append(frags, block, frag, instr);
      }

      instr = next;

    // Extend block with this instruction and move to the next instruction.
    } else {
      auto next = instr->Next();
      frag = Append(frags, block, frag, instr);
      instr = next;
    }
  }
}

// Get the list of fragments associated with a basic block.
static CodeFragment *FragmentForBlock(FragmentListBuilder *frags,
                                      DecodedBasicBlock *block) {
  auto first_instr = block->FirstInstruction();
  CodeFragment *frag = GetMetaData<CodeFragment *>(first_instr);
  if (!frag) {
    auto label = new LabelInstruction;
    frag = new CodeFragment;
    frag->attr.block_meta = block->UnsafeMetaData();
    frag->attr.is_block_head = true;
    frag->instrs.Append(label);
    SetMetaData(label, frag);
    SetMetaData(first_instr, frag);
    frags->Append(frag);

    // Start from the second instruction because the first instruction is a
    // annotation for beginning the basic block.
    ExtendFragment(frags, frag, block, first_instr->Next());
  }
  return frag;
}

}  // namespace

// Build a fragment list out of a set of basic blocks.
void BuildFragmentList(ContextInterface *context, LocalControlFlowGraph *cfg,
                       FragmentList *frags) {
  for (auto block : cfg->Blocks()) {
    auto decoded_block = DynamicCast<DecodedBasicBlock *>(block);
    if (decoded_block) {
      for (auto instr : decoded_block->Instructions()) {
        instr->ClearMetaData();
      }
    }
  }
  FragmentListBuilder builder{context, cfg, frags};
  FragmentForBlock(&builder, cfg->EntryBlock());
}

}  // namespace granary
