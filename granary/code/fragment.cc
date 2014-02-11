/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/fragment.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

namespace granary {

// Initialize the fragment to know about its specific block.
Fragment::Fragment(DecodedBasicBlock *block_)
    : next(nullptr),
      block(block_),
      first(nullptr),
      last(nullptr),
      size(-1) {
  block->AddFragment();
}

// Add an instruction into the fragment.
void Fragment::Append(Instruction *in) {
  if (!first) {
    first = in;
  }
  last = in;
}

// Returns the estimated size of the fragment. This should always be a
// pessimistic estimate, but sometimes might be correct.
int Fragment::Size(void) {
  if (-1 < size) {
    return size;
  }

  size = 0;

  return size;
}

// Append an individual fragment to a fragment list.
void FragmentList::Append(Fragment *frag) {
  if (!first) {
    first = frag;
    last = frag;
  } else {
    last->next = frag;
    last = frag;
  }
}

namespace {

static void ScheduleBlock(FragmentList &list, DecodedBasicBlock *block);

// Schedule a control-flow instruction into a fragment. Try to arrange for as
// much code to occur in a straight line by recursively scheduling blocks
// targeted by function calls and direct jumps.
static Fragment *ScheduleCFI(FragmentList &list, Fragment *fragment,
                             ControlFlowInstruction *cfi) {
  auto target = DynamicCast<DecodedBasicBlock *>(cfi->TargetBlock());
  if (!target || target->IsScheduled() || cfi->IsConditionalJump()) {
    return fragment;
  }

  GRANARY_ASSERT(cfi->IsFunctionCall() || cfi->IsUnconditionalJump());

  // Function call with direct target. Add in a special annotation for eliding
  // the function call by pushing on return address.
  if (cfi->IsFunctionCall()) {
    auto annotation = new AnnotationInstruction(
        PUSH_FUNCTION_RETURN_ADDRESS, cfi->Next());
    cfi->InsertBefore(std::unique_ptr<Instruction>(annotation));
    fragment->Append(annotation);

  // Unconditional jump; make sure the fragment ends in the jump, we'll elide
  // it later if possible.
  } else {
    fragment->Append(cfi);
  }

  ScheduleBlock(list, target);
  return nullptr;  // Split the fragment into two parts.
}

// Schedule the instructions of a decoded basic block into the list of
// fragments.
static void ScheduleBlock(FragmentList &list, DecodedBasicBlock *block) {
  if (!block || block->IsScheduled()) {
    return;
  }

  Fragment *fragment(nullptr);
  bool is_unreachable_code(false);

  for (auto instr : block->Instructions()) {

    // Don't include unreachable instructions. However, if we come across the
    // target of a branch then we'll treat is as reachable again.
    if (is_unreachable_code) {
      auto annotation = DynamicCast<const AnnotationInstruction *>(instr);
      if (annotation && annotation->IsBranchTarget()) {
        is_unreachable_code = false;
      } else {
        continue;
      }
    }

    // Make sure we have a fragment to which we can schedule instructions.
    if (!fragment) {
      fragment = new Fragment(block);
      list.Append(fragment);
    }

    auto cfi = DynamicCast<ControlFlowInstruction *>(instr);
    if (cfi) {
      fragment = ScheduleCFI(list, fragment, cfi);

      // If we're passing an instruction that control cannot pass, then treat
      // the remainder of the code as unreachable. Code after an unreachable
      // instruction can eventually become reachable by being targeted by a
      // branch instruction. This affects the next instruction.
      is_unreachable_code = is_unreachable_code || cfi->IsUnconditionalJump() ||
                            cfi->IsFunctionReturn() || cfi->IsSystemReturn() ||
                            cfi->IsInterruptReturn();
    }

    if (fragment) {
      fragment->Append(instr);
    }
  }
}

}  // namespace

// Schedule the blocks of an LCFG for allocation. This means splitting the
// instruction lists of blocks into one or more fragments of instruction lists,
// such that a given blocks instructions may be discontinuous.
FragmentList ScheduleBlocks(const LocalControlFlowGraph *cfg) {
  FragmentList list = {nullptr, nullptr};
  for (auto block : cfg->Blocks()) {
    ScheduleBlock(list, DynamicCast<DecodedBasicBlock *>(block));
  }
  return list;
}

}  // namespace granary

