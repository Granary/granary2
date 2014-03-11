/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"

namespace granary {

// Initialize the fragment from a basic block.
Fragment::Fragment(int id_)
    : fall_through_target(nullptr),
      branch_target(nullptr),
      branch_instr(nullptr),
      next(nullptr),
      id(id_),
      is_block_head(false),
      is_future_block_head(true),
      is_exit(false),
      block_meta(nullptr),
      first(nullptr),
      last(nullptr) {}

// Append an instruction into the fragment.
void Fragment::Append(std::unique_ptr<Instruction> instr) {
  if (last) {
    last = last->InsertAfter(std::move(instr));
  } else {
    last = first = instr.release();
  }
}

// Wraps up state that is used to build fragments.
class FragmentBuilder {
 public:
  inline FragmentBuilder(void)
      : next_id(0),
        native_fragment(nullptr),
        first(nullptr),
        next_frag(&first) {}

  // Get the list of fragments associated with a basic block.
  Fragment *FragmentForBlock(DecodedBasicBlock *block) {
    auto first_instr = block->FirstInstruction();
    Fragment *frag = first_instr->GetMetaData<Fragment *>();
    if (!frag) {
      auto label = new LabelInstruction;
      frag = MakeFragment();
      frag->block_meta = block->MetaData();
      frag->is_block_head = true;
      frag->first = frag->last = label;
      label->SetMetaData<Fragment *>(frag);
      first_instr->SetMetaData<Fragment *>(frag);
      ExtendFragment(frag, block, first_instr->Next());
    }
    return frag;
  }

 private:
  Fragment *MakeFragment(void) {
    auto frag = new Fragment(next_id++);
    *next_frag = frag;
    next_frag = &(frag->next);
    return frag;
  }

  // Make a fragment for a native basic block.
  Fragment *MakeNativeFragment(void) {
    if (!native_fragment) {
      native_fragment = MakeFragment();
      native_fragment->is_exit = true;
      // TODO(pag): Add hard constraints!
    }
    return native_fragment;
  }

  // Make a block head fragment for some kind of future basic block.
  Fragment *MakeFutureBlockFragment(InstrumentedBasicBlock *block) {
    auto frag = MakeFragment();
    frag->block_meta = block->MetaData();
    frag->is_exit = true;
    frag->is_future_block_head = true;
    return frag;
  }

  // Make a block head fragment for a cached basic block. This means importing
  // its register schedule as hard constraints.
  Fragment *MakeCachedFragment(CachedBasicBlock *block) {
    auto frag = MakeFragment();
    frag->block_meta = block->MetaData();
    frag->is_exit = true;
    // TODO(pag): Import constraints.
    return frag;
  }

  // Create a new fragment starting at a label.
  Fragment *MakeEmptyLabelFragment(DecodedBasicBlock *block,
                                   LabelInstruction *label) {
    auto frag = MakeFragment();
    frag->block_meta = block->MetaData();
    frag->Append(label->UnsafeUnlink());
    label->SetMetaData<Fragment *>(frag);
    return frag;
  }

  // Get or make the fragment starting at a label.
  Fragment *GetOrMakeLabelFragment(DecodedBasicBlock *block,
                                   LabelInstruction *label) {
    Fragment *frag = label->GetMetaData<Fragment *>();
    if (!frag) {
      auto next = label->Next();
      frag = MakeEmptyLabelFragment(block, label);
      ExtendFragment(frag, block, next);
    }
    return frag;
  }

  // Split a fragment into two at a label instruction `instr`. If the label
  // is already associated with a `Fragment` instance then set that fragment as
  // the fall-through of our current fragment. If new `Fragment` instance is
  // associated with the label, then create one, add the association, and
  // add the instructions following the label into the new fragment.
  void SplitFragmentAtLabel(Fragment *frag, DecodedBasicBlock *block,
                            Instruction *instr) {
    Fragment *label_fragment = instr->GetMetaData<Fragment *>();
    if (label_fragment) {  // Already processed this fragment.
      frag->fall_through_target = label_fragment;
    } else {
      auto label = DynamicCast<LabelInstruction *>(instr);
      auto next = instr->Next();
      frag->fall_through_target = MakeEmptyLabelFragment(block, label);
      ExtendFragment(frag->fall_through_target, block, next);
    }
  }

  // Split a fragment into two at a local branch instruction. First get or
  // create the fragment associated with the branch target. Then create a
  // fragment for the fall-through of the branch, and include remaining
  // instructions from the block into that fragment.
  void SplitFragmentAtBranch(Fragment *frag, DecodedBasicBlock *block,
                             Instruction *instr) {
    auto branch = DynamicCast<BranchInstruction *>(instr);
    auto label = branch->TargetInstruction();
    auto next = instr->Next();
    frag->Append(std::move(instr->UnsafeUnlink()));
    frag->branch_target = GetOrMakeLabelFragment(block, label);
    frag->branch_instr = branch;
    if (branch->IsConditionalJump()) {
      frag->fall_through_target = MakeEmptyLabelFragment(
          block, new LabelInstruction);
      ExtendFragment(frag->fall_through_target, block, next);
    }
  }

  // Return the fragment for a block that is targeted by a control-flow
  // instruction.
  Fragment *FragmentForTargetBlock(BasicBlock *block) {
    // Function/interrupt/system return. We can never be sure in any of
    // these cases if execution returns to the code cache, and even then,
    // meta-data doesn't flow to the targets of returns because it's never
    // clear to what context execution returns.
    //
    // OR:
    //
    // Direct call/jump to native; interrupt call, system call. All regs
    // must be homed on exit of this block lets things really screw up.
    if (IsA<ReturnBasicBlock *>(block) ||
        IsA<NativeBasicBlock *>(block)) {
      return MakeNativeFragment();

    // Indirect call/jump, or direct call/jump/conditional jump
    // to a future block.
    } else if (IsA<IndirectBasicBlock *>(block) ||
               IsA<DirectBasicBlock *>(block)) {
      return MakeFutureBlockFragment(
            DynamicCast<InstrumentedBasicBlock *>(block));

    // Direct call/jump/conditional jump to a decoded block.
    } else if (IsA<DecodedBasicBlock *>(block)) {
      return FragmentForBlock(DynamicCast<DecodedBasicBlock *>(block));

    // Direct call/jump/conditional jump to a cached block.
    } else {
      return MakeCachedFragment(DynamicCast<CachedBasicBlock *>(block));
    }
  }

  // Split a fragment at a non-local control-flow instruction.
  void SplitFragmentAtCFI(Fragment *frag, DecodedBasicBlock *block,
                          Instruction *instr) {
    auto cfi = DynamicCast<ControlFlowInstruction *>(instr);
    auto next = instr->Next();
    auto target_block = cfi->TargetBlock();

    frag->Append(std::move(instr->UnsafeUnlink()));
    frag->branch_instr = cfi;
    frag->branch_target = FragmentForTargetBlock(target_block);

    // If this was a call or a conditional jump then add a fall-through
    // fragment.
    if (cfi->IsFunctionCall() || cfi->IsInterruptCall() ||
        cfi->IsSystemCall() || cfi->IsConditionalJump()) {

      // Try to be smarter about the fall-through to avoid making "useless"
      // intermediate fragments containing only a single unconditional
      // jump.
      auto next_cfi = DynamicCast<ControlFlowInstruction *>(next);
      if (next_cfi && next_cfi->IsUnconditionalJump()) {
        target_block = next_cfi->TargetBlock();
        frag->fall_through_target = FragmentForTargetBlock(target_block);
      } else {
        auto label = new LabelInstruction;
        frag->fall_through_target = MakeEmptyLabelFragment(block, label);
        frag = frag->fall_through_target;
        label->SetMetaData<Fragment *>(frag);
        ExtendFragment(frag, block, next);
      }
    }
  }

  // Extend a fragment with the instructions from a particular basic block.
  // This might end up generating many more fragments.
  void ExtendFragment(Fragment *frag, DecodedBasicBlock *block,
                      Instruction *instr) {
    const auto last_instr = block->LastInstruction();
    for (; instr != last_instr; ) {
      // Treat every label as beginning a new fragment.
      if (IsA<LabelInstruction *>(instr)) {
        return SplitFragmentAtLabel(frag, block, instr);

      // Found a local branch; add in the fall-through and/or the branch
      // target.
      } else if (IsA<BranchInstruction *>(instr)) {
        return SplitFragmentAtBranch(frag, block, instr);

      // Found a non-local branch to a basic block.
      } else if (IsA<ControlFlowInstruction *>(instr)) {
        return SplitFragmentAtCFI(frag, block, instr);

      // Extend block with this instruction and move to the next instruction.
      } else {
        auto next = instr->Next();
        frag->Append(std::move(instr->UnsafeUnlink()));
        instr = next;
      }
    }
  }

  int next_id;

  Fragment *native_fragment;
  Fragment *first;
  Fragment **next_frag;
};

// Build a fragment for a
//static Fragment *BuildFragment(BasicBlock *block, int *num_fragments) {

  // In case of direct block, every edge to the same direct block *still* need
  // separate fragments (which are the edge code!). Each edge goes to a new
  // future fragment.

  // In the case of a function call, we can split the fragment into two around
  // the call into an intermediate fragment that goes to the fall-through jump,
  // and the intermediate fragment encodes the ABI return constraints that
  // are present immediately after the call.
  //      --> TODO is this a good idea?

  // In the case of an indirect block, the fragment can contain to code for
  // resolving the branch, all using virtual regs.

  // In case of a native basic block, we can jump to a fragment with fixed
  // incoming homing constraints, and the block is empty, because the jump/call
  // is all that was needed.

  // In the case of a cached basic block, we import its constraints into the
  // target block.
//}


// Build a fragment list out of a set of basic blocks.
Fragment *BuildFragmentList(LocalControlFlowGraph *cfg) {
  for (auto block : cfg->Blocks()) {
    auto decoded_block = DynamicCast<DecodedBasicBlock *>(block);
    if (decoded_block) {
      for (auto instr : decoded_block->Instructions()) {
        instr->ClearMetaData();
      }
    }
  }
  FragmentBuilder builder;
  return builder.FragmentForBlock(cfg->EntryBlock());
}

}  // namespace granary
