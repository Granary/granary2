/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"

#include "granary/code/assemble/fragment.h"
#include "granary/code/assemble/2_build_fragment_list.h"

#include "granary/util.h"

namespace granary {

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

// Try to add a flag split hint to a code fragment.
//
// Note: This function has an architecture-specific implementation.
extern void TryAddFlagSplitHint(CodeFragment *frag,
                                const NativeInstruction *instr);

// Returns true if this instruction can change the interrupt enabled state on
// this CPU.
//
// Note: This function has an architecture-specific implementation.
extern bool ChangesInterruptDeliveryState(const NativeInstruction *instr);

namespace {

// Make a new code fragment.
static CodeFragment *MakeFragment(FragmentList *frags) {
  auto frag = new CodeFragment;
  frags->Append(frag);
  return frag;
}

// Make a fragment for a native basic block.
static ExitFragment *MakeNativeFragment(FragmentList *frags) {
  auto frag = new ExitFragment(FRAG_EXIT_NATIVE);
  frags->Append(frag);
  return frag;
}

// Make a block head fragment for a cached basic block. This means importing
// its register schedule as hard constraints.
static ExitFragment *MakeCachedFragment(FragmentList *frags,
                                        CachedBasicBlock *block) {
  auto frag = new ExitFragment(FRAG_EXIT_EXISTING_BLOCK);
  frags->Append(frag);
  frag->block_meta = block->UnsafeMetaData();
  return frag;
}

// Create a new fragment starting at a label.
static CodeFragment *MakeEmptyLabelFragment(FragmentList *frags,
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
static CodeFragment *Append(FragmentList *frags, DecodedBasicBlock *block,
                            CodeFragment *frag, Instruction *instr) {
  instr->UnsafeUnlink().release();
  if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
    if (ninstr->WritesConditionCodes()) {

      // Try to split the fragment before appending the instruction so that
      // we can (hopefully) optimize on some flag save/restore code.
      if (frag->attr.has_flag_split_hint && !ninstr->IsAppInstruction()) {
        GRANARY_ASSERT(!frag->attr.modifies_flags);
        frag->attr.is_app_code = true;
        auto succ = MakeEmptyLabelFragment(frags, block, new LabelInstruction);
        frag->successors[FRAG_SUCC_FALL_THROUGH] = succ;
        frag = succ;
      }
      frag->attr.modifies_flags = true;
      frag->attr.has_flag_split_hint = false;
    }
    frag->attr.has_native_instrs = true;

    // If the app instruction reads/writes the flags, then to maintain the
    // flag save/restore invariant, we must make this into an app fragment.
    // Also, if the app instruction writes to the stack pointer, then we want
    // to try to prevent such an instruction from being placed inside of a
    // flag save/restore zone.
    if (!frag->attr.is_app_code && InstrMakesFragmentIntoAppCode(ninstr)) {
      frag->attr.is_app_code = true;
      frag->attr.has_flag_split_hint = false;
    }
    if (!frag->attr.is_app_code && !frag->attr.has_flag_split_hint) {
      TryAddFlagSplitHint(frag, ninstr);
    }
  }
  frag->instrs.Append(instr);
  return frag;
}

// Extend a fragment with the instructions from a particular basic block.
// This might end up generating many more fragments.
static void ExtendFragment(FragmentList *frags, CodeFragment *frag,
                           DecodedBasicBlock *block, Instruction *instr);

// Get or make the fragment starting at a label.
static CodeFragment *GetOrMakeLabelFragment(FragmentList *frags,
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
static void SplitFragmentAtLabel(FragmentList *frags, CodeFragment *frag,
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
static void SplitFragmentAtBranch(FragmentList *frags, CodeFragment *frag,
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
static CodeFragment *FragmentForBlock(FragmentList *frags,
                                      DecodedBasicBlock *block);


// Make an edge fragment and an exit fragment for some future block.
static CodeFragment *MakeEdgeFragment(FragmentList *frags,
                                      CodeFragment *pred_frag,
                                      InstrumentedBasicBlock *block,
                                      BlockMetaData *block_meta) {
  auto edge_frag = new CodeFragment;
  edge_frag->stack.is_valid = pred_frag->stack.is_valid;
  edge_frag->stack.is_checked = pred_frag->stack.is_checked;
  edge_frag->attr.is_edge_code = true;
  edge_frag->attr.block_meta = block_meta;

  auto exit_frag = new ExitFragment(FRAG_EXIT_FUTURE_BLOCK);
  exit_frag->block_meta = block_meta;

  // If this is the target of an indirect CFI (call, jmp, ret) then make sure
  // that the edge code shares the same partition as the predecessor so that
  // virtual registers can be spread across both.
  if (!IsA<DirectBasicBlock *>(block)) {
    edge_frag->partition.Union(edge_frag, pred_frag);
  }

  edge_frag->successors[0] = exit_frag;
  frags->Append(edge_frag);
  frags->Append(exit_frag);

  return edge_frag;
}

static ExitFragment *MakeExitFragment(FragmentList *frags) {
  auto exit_frag = new ExitFragment(FRAG_EXIT_NATIVE);
  frags->Append(exit_frag);
  return exit_frag;
}

// Return the fragment for a block that is targeted by a control-flow
// instruction.
static Fragment *FragmentForTargetBlock(FragmentList *frags,
                                        CodeFragment *pred_frag,
                                        BasicBlock *block) {
  // Function/interrupt/system return. In these cases, we can't be sure (at
  // instrumentation time) that execution returns to the code cache.
  //
  // OR:
  //
  // Direct call/jump to native; interrupt call, system call. All regs
  // must be homed on exit of this block lets things really screw up.
  if (IsA<NativeBasicBlock *>(block)) {
    return MakeNativeFragment(frags);

  // Direct call/jump/conditional jump to a decoded block.
  } else if (IsA<DecodedBasicBlock *>(block)) {
    return FragmentForBlock(frags, DynamicCast<DecodedBasicBlock *>(block));

  // Direct call/jump/conditional jump to a cached block.
  } else if (IsA<CachedBasicBlock *>(block)) {
    return MakeCachedFragment(frags, DynamicCast<CachedBasicBlock *>(block));

  // Direct jump / conditional jump to an unresolved block, need to add in
  // some edge code.
  } else if (auto direct_block = DynamicCast<DirectBasicBlock *>(block)) {
    return MakeEdgeFragment(frags, pred_frag, direct_block,
                            direct_block->MetaData());

  // Indirect call/jump, or direct call/jump/conditional jump
  // to a future block.
  } else if (IsA<ReturnBasicBlock *>(block) ||
             IsA<IndirectBasicBlock *>(block)) {
    auto inst_block = DynamicCast<InstrumentedBasicBlock *>(block);
    auto block_meta = inst_block->UnsafeMetaData();
    if (block_meta) {
      return MakeEdgeFragment(frags, pred_frag, inst_block, block_meta);
    } else {
      return MakeExitFragment(frags);
    }
  } else {
    GRANARY_ASSERT(false);
    return nullptr;
  }
}

// Go find the next instruction that is a control-flow instruction. If we find
// any non-control-flow native instructions between here and the next
// instruction then return `nullptr`.
static ControlFlowInstruction *NextUninterruptedCFI(Instruction *next) {
  for (auto instr : InstructionListIterator(next)) {
    if (auto cfi = DynamicCast<ControlFlowInstruction *>(instr)) {
      return cfi;
    } else if (IsA<NativeInstruction *>(instr)) {
      break;
    }
  }
  return nullptr;
}

// Append a CFI to a fragment, and potentially make a new fragment for the CFI.
//
// TODO(pag): Clean up this function; it is incredibly complicated and I'm
//            unlikely to remember all the nuances in the future.
static CodeFragment *AppendCFI(FragmentList *frags, CodeFragment *frag,
                               DecodedBasicBlock *block,
                               Fragment *target_frag,
                               ControlFlowInstruction *cfi) {
  CodeFragment *ret_frag(frag);
  bool makes_stack_valid = cfi->IsFunctionCall() || cfi->IsFunctionReturn() ||
                           cfi->IsInterruptReturn();
  bool targets_edge_code = false;
  bool can_add_to_partition = true;
  if (auto target_cfrag = DynamicCast<CodeFragment *>(target_frag)) {
    targets_edge_code = target_cfrag->attr.is_edge_code;
  }
  if (frag->attr.has_native_instrs) {
    // This CFI is not compatible with the current fragment because this CFI
    // makes the stack valid, but the `frag`s stack isn't valid.
    if (makes_stack_valid && frag->stack.is_checked && !frag->stack.is_valid) {
      ret_frag = nullptr;
    }

    // Force things like direct jumps and calls into their own fragments.
    // Indirect jumps, indirect calls, and specialized returns can be placed
    // in the same fragment as other code because the edge code will share
    // virtual register scope with the instructions before the jump/call/return.
    if (!cfi->HasIndirectTarget() && !cfi->IsConditionalJump()) {
      ret_frag = nullptr;
      if (targets_edge_code) {
        can_add_to_partition = false;
      }
    }
  }

  // Handle cases like unspecialized returnes, interrupt returns, and system
  // returns, making sure that we never put them in the same partition with
  // some other code, otherwise we risk saving/restoring regs before/after this
  // instruction, and control will never reach "after" this instruction as it
  // will have gone somewhere else.
  if (!targets_edge_code && cfi->instruction.WritesToStackPointer()) {
    if (frag->attr.has_native_instrs) ret_frag = nullptr;
    can_add_to_partition = false;
  }

  // We need to add a new fragment for this CFI.
  if (!ret_frag) {
    auto label = new LabelInstruction;
    auto succ = MakeEmptyLabelFragment(frags, block, label);
    frag->successors[0] = succ;
    ret_frag = succ;
  }

  // This CFI is something like a function call / return, i.e. it makes the
  // stack pointer appear to point to a C-style call stack.
  if (makes_stack_valid) {
    ret_frag->stack.is_valid = true;
    ret_frag->stack.is_checked = true;
  }
  ret_frag->attr.can_add_to_partition = can_add_to_partition;
  ret_frag = Append(frags, block, ret_frag, cfi);
  if (targets_edge_code) {
    ret_frag->attr.branches_to_edge_code = true;
  }
  return ret_frag;
}

// Split a fragment at a non-local control-flow instruction.
static void SplitFragmentAtCFI(FragmentList *frags, CodeFragment *frag,
                               DecodedBasicBlock *block,
                               ControlFlowInstruction *cfi) {
  auto next = cfi->Next();
  auto target_block = cfi->TargetBlock();
  auto is_branch = !cfi->IsUnconditionalJump() || cfi->HasIndirectTarget();

  if (is_branch) {
    auto target_frag = FragmentForTargetBlock(frags, frag, target_block);
    frag = AppendCFI(frags, frag, block, target_frag, cfi);
    frag->successors[FRAG_SUCC_BRANCH] = target_frag;
    frag->branch_instr = cfi;
    if (cfi->IsFunctionReturn() || cfi->IsInterruptReturn() ||
        cfi->IsSystemReturn()) {
      return;
    }
  } else {
    next = cfi;  // Pretend that direct jumps are just fall-throughs.
  }

  // Try to be smarter about the fall-through to avoid making "useless"
  // intermediate fragments containing only a single unconditional
  // jump.
  auto next_cfi = NextUninterruptedCFI(next);
  if (next_cfi && next_cfi->IsUnconditionalJump()) {
    target_block = next_cfi->TargetBlock();
    frag->successors[0] = FragmentForTargetBlock(frags, frag, target_block);
  } else {
    auto label = new LabelInstruction;
    auto succ = MakeEmptyLabelFragment(frags, block, label);
    frag->successors[0] = succ;
    succ->attr.block_meta = frag->attr.block_meta;
    ExtendFragment(frags, succ, block, next);
  }
}

// Split a fragment at a place where the validness of the stack pointer changes
// from defined to undefined, or undefined to defined.
static void SplitFragmentAtStackChange(FragmentList *frags, CodeFragment *frag,
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
static void SplitFragment(FragmentList *frags, CodeFragment *frag,
                          DecodedBasicBlock *block, Instruction *next) {
  auto label = new LabelInstruction;
  auto succ = MakeEmptyLabelFragment(frags, block, label);
  frag->successors[0] = succ;
  ExtendFragment(frags, succ, block, next);
}

// Split a fragment at an instruction that changes the interrupt state.
static void SplitFragmentAtInterruptChange(FragmentList *frags,
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
static void ExtendFragment(FragmentList *frags, CodeFragment *frag,
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
      if (ChangesInterruptDeliveryState(ninstr)) {
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
        if (frag->stack.is_checked && !frag->stack.is_valid) {
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
      // This annotation is somewhat more suble than the above two. The idea
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
      } else if (IA_UNKNOWN_STACK == annot->annotation) {
        GRANARY_ASSERT(!frag->stack.is_checked || !frag->stack.is_valid);
        frag->stack.is_checked = true;
        frag->stack.is_valid = false;
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
static CodeFragment *FragmentForBlock(FragmentList *frags,
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
void BuildFragmentList(LocalControlFlowGraph *cfg, FragmentList *frags) {
  for (auto block : cfg->Blocks()) {
    auto decoded_block = DynamicCast<DecodedBasicBlock *>(block);
    if (decoded_block) {
      for (auto instr : decoded_block->Instructions()) {
        instr->ClearMetaData();
      }
    }
  }
  FragmentForBlock(frags, cfg->EntryBlock());
}

}  // namespace granary
