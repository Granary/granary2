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

// Make a block head fragment for some kind of future basic block.
static ExitFragment *MakeFutureBlockFragment(FragmentList *frags,
                                             InstrumentedBasicBlock *block) {
  auto frag = new ExitFragment(FRAG_EXIT_FUTURE_BLOCK);
  frags->Append(frag);
  frag->block_meta = block->UnsafeMetaData();
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
  return instr->instruction.WritesToStackPointer() ||
         instr->ReadsConditionCodes() ||
         instr->WritesConditionCodes();
}

// Append an instruction to a fragment. This also does minor flag usage analysis
// of the instruction to figure out if the fragment should be treated as an
// application fragment or an instrumentation code fragment.
static void Append(CodeFragment *frag, Instruction *instr) {
  instr->UnsafeUnlink().release();
  if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
    frag->attr.has_native_instrs = true;
    if (ninstr->WritesConditionCodes()) {
      frag->attr.modifies_flags = true;
    }

    // If the app instruction reads/writes the flags, then to maintain the
    // flag save/restore invariant, we must make this into an app fragment.
    // Also, if the app instruction writes to the stack pointer, then we want
    // to try to prevent such an instruction from being placed inside of a
    // flag save/restore zone.
    if (!frag->attr.is_app_code && InstrMakesFragmentIntoAppCode(ninstr)) {
      frag->attr.is_app_code = true;
    }
  }
  frag->instrs.Append(instr);
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
    Append(frag, branch);
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
    Append(frag, label);
    ExtendFragment(frags, frag, block, next);
  }
}

// Get the list of fragments associated with a basic block.
static CodeFragment *FragmentForBlock(FragmentList *frags,
                                      DecodedBasicBlock *block);

// Return the fragment for a block that is targeted by a control-flow
// instruction.
static Fragment *FragmentForTargetBlock(FragmentList *frags,
                                        BasicBlock *block) {
  // Function/interrupt/system return. In these cases, we can't be sure (at
  // instrumentationin time) that execution returns to the code cache.
  //
  // OR:
  //
  // Direct call/jump to native; interrupt call, system call. All regs
  // must be homed on exit of this block lets things really screw up.
  if (IsA<NativeBasicBlock *>(block)) {
    return MakeNativeFragment(frags);

  // Indirect call/jump, or direct call/jump/conditional jump
  // to a future block.
  } else if (IsA<ReturnBasicBlock *>(block) ||
             IsA<IndirectBasicBlock *>(block) ||
             IsA<DirectBasicBlock *>(block)) {
    return MakeFutureBlockFragment(
        frags, DynamicCast<InstrumentedBasicBlock *>(block));

  // Direct call/jump/conditional jump to a decoded block.
  } else if (IsA<DecodedBasicBlock *>(block)) {
    return FragmentForBlock(frags, DynamicCast<DecodedBasicBlock *>(block));

  // Direct call/jump/conditional jump to a cached block.
  } else {
    return MakeCachedFragment(frags, DynamicCast<CachedBasicBlock *>(block));
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

// Split a fragment at a non-local control-flow instruction.
static void SplitFragmentAtCFI(FragmentList *frags, CodeFragment *frag,
                               DecodedBasicBlock *block,
                               ControlFlowInstruction *cfi) {
  auto next = cfi->Next();
  auto target_block = cfi->TargetBlock();
  auto is_direct_jump = cfi->IsUnconditionalJump() &&
                        !cfi->HasIndirectTarget();
  if (!is_direct_jump) {
    Append(frag, cfi);
    frag->successors[FRAG_SUCC_BRANCH] = FragmentForTargetBlock(frags,
                                                                target_block);
    if (cfi->IsFunctionReturn() || cfi->IsInterruptReturn() ||
        cfi->IsSystemReturn()) {
      std::swap(frag->successors[FRAG_SUCC_FALL_THROUGH],
                frag->successors[FRAG_SUCC_BRANCH]);
      return;
    } else {
      frag->branch_instr = cfi;
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
    frag->successors[0] = FragmentForTargetBlock(frags, target_block);
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
      if (frag->attr.is_app_code) {
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

      // Need to put things like function/interrupt call/return into their own
      // fragments, because later partitioning can't then arrange to
      // deallocate virtual registers after a function call, or after a
      // return.
      if (cfi->instruction.WritesToStackPointer()) {
        if (frag->attr.has_native_instrs ||
            (frag->stack.is_checked && !frag->stack.is_valid)) {
          auto label = new LabelInstruction;
          auto succ = MakeEmptyLabelFragment(frags, block, label);
          frag->successors[0] = succ;
          frag = succ;
        }

        frag->stack.is_checked = true;
        frag->stack.is_valid = true;
        frag->stack.has_stack_changing_cfi = true;
      }
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
        if (frag->stack.is_checked && frag->stack.is_valid) {
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
      }
      instr = next;

    // Extend block with this instruction and move to the next instruction.
    } else {
      auto next = instr->Next();
      Append(frag, instr);
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
