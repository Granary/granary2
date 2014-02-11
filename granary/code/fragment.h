/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_FRAGMENT_H_
#define GRANARY_CODE_FRAGMENT_H_

#ifndef GRANARY_INTERNAL
# error "Only available to internal Granary code."
#endif

#include "granary/base/list.h"
#include "granary/base/new.h"

namespace granary {

// Forward declarations.
class DecodedBasicBlock;
class Instruction;
class LocalControlFlowGraph;

// List of instructions that are from a basic block. A given basic block is
// typically represented by a single fragment; however, some basic blocks are
// split across multiple fragments.
class Fragment {
 public:
  // Initialize the fragment to know about its specific block.
  explicit Fragment(DecodedBasicBlock *block_);

  // Add an instruction into the fragment.
  void Append(Instruction *in);

  // Returns the estimated size of the fragment. This should always be a
  // pessimistic estimate, but sometimes might be correct.
  int Size(void);

  GRANARY_DEFINE_NEW_ALLOCATOR(Fragment, {
    SHARED = true,
    ALIGNMENT = 1
  })

  Fragment *next;

 private:
  Fragment(void) = delete;

  // Block and pointers into a sub-list of its instructions.
  DecodedBasicBlock *block;
  Instruction *first;
  Instruction *last;  // Inclusive.
  int size;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Fragment);
};

// Iterator over fragments.
typedef LinkedListIterator<Fragment> FragmentIterator;

// List of scheduled fragments. Each fragments contains a sequence of zero or
// more instructions to encode.
struct FragmentList {
  Fragment *first;
  Fragment *last;

  // Return an iterator over all fragments in this fragment list.
  inline FragmentIterator Fragments(void) {
    return FragmentIterator(first);
  }

  // Append an individual fragment to a fragment list.
  void Append(Fragment *frag);
};

// Schedule the blocks of an LCFG for allocation. This means splitting the
// instruction lists of blocks into one or more fragments of instruction lists,
// such that a given blocks instructions may be discontinuous.
FragmentList ScheduleBlocks(const LocalControlFlowGraph *cfg);

}  // namespace granary

#endif  // GRANARY_CODE_FRAGMENT_H_
