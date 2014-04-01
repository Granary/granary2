/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/instruction.h"

#include "granary/code/assemble/fragment.h"
#include "granary/code/assemble/2_build_fragment_list.h"

#include "granary/util.h"

namespace granary {

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
    Fragment *frag = GetMetaData<Fragment *>(first_instr);
    if (!frag) {
      auto label = new LabelInstruction;
      frag = MakeFragment();
      frag->block_meta = block->MetaData();
      frag->is_decoded_block_head = true;
      frag->first = frag->last = label;
      SetMetaData(label, frag);
      SetMetaData(first_instr, frag);
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
    frag->block_meta = block->UnsafeMetaData();
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
    frag->AppendInstruction(label->UnsafeUnlink());
    SetMetaData(label, frag);
    return frag;
  }

  // Get or make the fragment starting at a label.
  Fragment *GetOrMakeLabelFragment(DecodedBasicBlock *block,
                                   LabelInstruction *label) {
    Fragment *frag = GetMetaData<Fragment *>(label);
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
    Fragment *label_fragment = GetMetaData<Fragment *>(instr);
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

    if (branch->IsConditionalJump()) {
      frag->AppendInstruction(std::move(instr->UnsafeUnlink()));
      frag->branch_instr = branch;
      frag->fall_through_target = MakeEmptyLabelFragment(
          block, new LabelInstruction);
      ExtendFragment(frag->fall_through_target, block, next);
      frag->branch_target = GetOrMakeLabelFragment(block, label);
    } else {
      frag->fall_through_target = GetOrMakeLabelFragment(block, label);
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
    if (IsA<NativeBasicBlock *>(block)) {
      return MakeNativeFragment();

    // Indirect call/jump, or direct call/jump/conditional jump
    // to a future block.
    } else if (IsA<ReturnBasicBlock *>(block) ||
               IsA<IndirectBasicBlock *>(block) ||
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
    auto is_direct_jump = cfi->IsUnconditionalJump() &&
                          !cfi->HasIndirectTarget();
    if (!is_direct_jump) {
      frag->AppendInstruction(std::move(instr->UnsafeUnlink()));
      frag->branch_instr = cfi;
      frag->branch_target = FragmentForTargetBlock(target_block);
      if (cfi->IsFunctionReturn() || cfi->IsInterruptReturn() ||
          cfi->IsSystemReturn()) {
        return;
      }
    // Pretend that direct jumps are just fall-throughs.
    } else {
      next = instr;
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
        frag->fall_through_target = FragmentForTargetBlock(target_block);
      } else {
        auto label = new LabelInstruction;
        frag->fall_through_target = MakeEmptyLabelFragment(block, label);
        ExtendFragment(frag->fall_through_target, block, next);
      }
    }
  }

  // Split a fragment at a stack pointer-changing instruction.
  void SplitFragmentAtStackChange(Fragment *frag, DecodedBasicBlock *block,
                                  Instruction *next) {
    auto label = new LabelInstruction;
    frag->fall_through_target = MakeEmptyLabelFragment(block, label);
    ExtendFragment(frag->fall_through_target, block, next);
  }

  // Split a fragment at a point where the instructions in the block change
  // from instrumentation-added -> app, or app -> instrumentation added.
  void SplitFragmentAtAppChange(Fragment *frag, DecodedBasicBlock *block,
                                Instruction *next) {
    auto label = new LabelInstruction;
    frag->fall_through_target = MakeEmptyLabelFragment(block, label);
    ExtendFragment(frag->fall_through_target, block, next);
  }

  // Extend a fragment with the instructions from a particular basic block.
  // This might end up generating many more fragments.
  void ExtendFragment(Fragment *frag, DecodedBasicBlock *block,
                      Instruction *instr) {
    const auto last_instr = block->LastInstruction();
    auto prev_instr_is_app = false;
    for (auto seen_first_instr(false); instr != last_instr; ) {

      // Treat every label as beginning a new fragment.
      if (IsA<LabelInstruction *>(instr)) {
        return SplitFragmentAtLabel(frag, block, instr);
      }

      // Split instructions into fragments such that fragments contain either
      // all native instructions, or all instrumentation instructions, but not
      // both. This splitting is used in a later stage to allow us to reason
      // about saving/restoring flags state between two native instructions
      // that are separated by instrumentation instructions.
      if (auto ninstr = DynamicCast<NativeInstruction *>(instr)) {
        if (!seen_first_instr) {
          seen_first_instr = true;
          prev_instr_is_app = ninstr->IsAppInstruction();
        } else if (ninstr->IsAppInstruction() != prev_instr_is_app) {
          return SplitFragmentAtAppChange(frag, block, instr);
        }
      }

      // Found a local branch; add in the fall-through and/or the branch
      // target.
      if (IsA<BranchInstruction *>(instr)) {
        return SplitFragmentAtBranch(frag, block, instr);

      // Found a non-local branch to a basic block.
      } else if (IsA<ControlFlowInstruction *>(instr)) {
        return SplitFragmentAtCFI(frag, block, instr);

      } else {
        // Extend block with this instruction and move to the next instruction.
        auto next = instr->Next();
        frag->AppendInstruction(std::move(instr->UnsafeUnlink()));

        // Break this fragment if it changes the stack pointer.
        auto ninstr = DynamicCast<NativeInstruction *>(instr);
        if (ninstr && ninstr->instruction.WritesToStackPointer()) {
          frag->writes_to_stack_pointer = true;
          frag->reads_from_stack_pointer = \
              ninstr->instruction.ReadsFromStackPointer();
          return SplitFragmentAtStackChange(frag, block, next);
        }

        instr = next;
      }
    }
  }

  int next_id;

  Fragment *native_fragment;
  Fragment *first;
  Fragment **next_frag;
};

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
