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
  frag->block_metadata = block->UnsafeMetaData();
  return frag;
}

// Make a block head fragment for a cached basic block. This means importing
// its register schedule as hard constraints.
static ExitFragment *MakeCachedFragment(FragmentList *frags,
                                        CachedBasicBlock *block) {
  auto frag = new ExitFragment(FRAG_EXIT_EXISTING_BLOCK);
  frags->Append(frag);
  frag->block_metadata = block->UnsafeMetaData();
  return frag;
}

// Create a new fragment starting at a label.
static CodeFragment *MakeEmptyLabelFragment(FragmentList *frags,
                                            DecodedBasicBlock *block,
                                            LabelInstruction *label) {
  auto frag = MakeFragment(frags);
  frag->block_metadata = block->MetaData();
  frag->instrs.Append(label->UnsafeUnlink().release());
  SetMetaData(label, frag);
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
  } else {
    auto label = DynamicCast<LabelInstruction *>(instr);
    auto next = instr->Next();
    auto succ = MakeEmptyLabelFragment(frags, block, label);
    frag->successors[0] = succ;
    ExtendFragment(frags, succ, block, next);
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
  if (branch->IsConditionalJump()) {
    frag->instrs.Append(branch->UnsafeUnlink().release());
    auto succ = MakeEmptyLabelFragment(frags, block, new LabelInstruction);
    frag->successors[0] = succ;
    ExtendFragment(frags, succ, block, next);
    frag->successors[1] = GetOrMakeLabelFragment(frags, block, label);
  } else {
    frag->successors[0] = GetOrMakeLabelFragment(frags, block, label);
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

// Split a fragment at a non-local control-flow instruction.
static void SplitFragmentAtCFI(FragmentList *frags, CodeFragment *frag,
                               DecodedBasicBlock *block,
                               ControlFlowInstruction *cfi) {
  auto next = cfi->Next();
  auto target_block = cfi->TargetBlock();
  auto is_direct_jump = cfi->IsUnconditionalJump() &&
                        !cfi->HasIndirectTarget();
  if (!is_direct_jump) {
    frag->instrs.Append(cfi->UnsafeUnlink().release());
    frag->successors[1] = FragmentForTargetBlock(frags, target_block);
    if (cfi->IsFunctionReturn() || cfi->IsInterruptReturn() ||
        cfi->IsSystemReturn()) {
      return;
    }
  // Pretend that direct jumps are just fall-throughs.
  } else {
    next = cfi;
  }

  // If this was a call or a conditional jump then add a fall-through
  // fragment.
  if (cfi->IsFunctionCall() || cfi->IsInterruptCall() ||
      cfi->IsSystemCall() || cfi->IsConditionalJump() ||
      is_direct_jump) {

    // Try to be smarter about the fall-through to avoid making "useless"
    // intermediate fragments containing only a single unconditional
    // jump.
    auto next_cfi = DynamicCast<ControlFlowInstruction *>(next);
    if (next_cfi && next_cfi->IsUnconditionalJump()) {
      target_block = next_cfi->TargetBlock();
      frag->successors[0] = FragmentForTargetBlock(frags, target_block);
    } else {
      auto label = new LabelInstruction;
      auto succ = MakeEmptyLabelFragment(frags, block, label);
      frag->successors[0] = succ;
      ExtendFragment(frags, succ, block, next);
    }
  }
}

// Split a fragment at a point where the instructions in the block change
// from instrumentation-added -> app, or app -> instrumentation added.
static void SplitFragmentAtAppChange(FragmentList *frags, CodeFragment *frag,
                                     DecodedBasicBlock *block,
                                     Instruction *next) {
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
  auto prev_native_instr_is_app = false;
  for (auto seen_first_native_instr(false); instr != last_instr; ) {

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
      if (!seen_first_native_instr) {
        seen_first_native_instr = true;
        prev_native_instr_is_app = ninstr->IsAppInstruction();
      } else if (ninstr->IsAppInstruction() != prev_native_instr_is_app &&
                 (ninstr->ReadsConditionCodes() ||
                  ninstr->WritesConditionCodes())) {
        return SplitFragmentAtAppChange(frags, frag, block, instr);
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
        auto label = new LabelInstruction;
        auto succ = MakeEmptyLabelFragment(frags, block, label);
        frag->successors[0] = succ;
        frag = succ;
      }
      return SplitFragmentAtCFI(frags, frag, block, cfi);

    } else {
      // Extend block with this instruction and move to the next instruction.
      auto next = instr->Next();
      frag->instrs.Append(instr->UnsafeUnlink().release());
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
    frag->is_app_code = false;
    frag->block_metadata = block->MetaData();
    frag->is_block_head = true;
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
