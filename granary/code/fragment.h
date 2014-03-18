/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_FRAGMENT_H_
#define GRANARY_CODE_FRAGMENT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/base.h"

#include "granary/base/bitset.h"
#include "granary/base/list.h"
#include "granary/base/new.h"

#include "granary/code/register.h"

namespace granary {

// Forward declarations.
class Instruction;
class LabelInstruction;
class NativeInstruction;
class DecodedBasicBlock;
class LocalControlFlowGraph;
class BlockMetaData;
class FragmentBuilder;

// Represents a basic block in the true sense. Granary basic blocks can contain
// local control flow, so they need to be split into fragments of instructions
// that more closely represent the actual run-time control flow. This lower
// level model is needed for register allocation, etc.
class Fragment {
 public:

  // Next fragment in the fragment list. This is always associated with an
  // implicit control-flow instruction between two fragments.
  Fragment *fall_through_target;

  // Conditional branch target. This always associated with an explicit
  // control-flow instruction between two fragments.
  Fragment *branch_target;
  NativeInstruction *branch_instr;

  // All fragments are chained together into a list for simple iteration,
  // freeing, etc.
  Fragment *next;

  // Unique ID of this fragment.
  const int id;

  // Is this block the first fragment in a decoded basic block?
  bool is_block_head;

  // Is this a future basic block?
  bool is_future_block_head;

  // Is this an exit block? An exit block is a future block, or a block that
  // ends in some kind of return, or a native block.
  bool is_exit;

  // Did the previous current data-flow pass change anything?
  bool data_flow_changed;

  // Does the last instruction in this fragment change the stack pointer?
  bool changes_stack_pointer;

  // Source basic block info.
  BlockMetaData *block_meta;

  // Instruction list.
  Instruction *first;
  Instruction *last;

  // Which physical registers are live on entry to this block.
  RegisterUsageTracker entry_regs_live;

 private:
  friend class FragmentBuilder;

  Fragment(void) = delete;

  // Initialize the fragment from a basic block.
  explicit Fragment(int id_);

  GRANARY_DEFINE_NEW_ALLOCATOR(Fragment, {
    SHARED = true,
    ALIGNMENT = 1
  })

  // Append an instruction into the fragment.
  void Append(std::unique_ptr<Instruction> instr);
};

typedef LinkedListIterator<Fragment> FragmentIterator;

// Build a fragment list out of a set of basic blocks.
Fragment *BuildFragmentList(LocalControlFlowGraph *cfg);

}  // namespace granary

#endif  // GRANARY_CODE_FRAGMENT_H_
