/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

namespace granary {

void ForwardInstructionIterator::operator++(void) {
  instr = instr->Next();
}

void BackwardInstructionIterator::operator++(void) {
  instr = instr->Previous();
}

// Move the iterator to the next basic block.
void BasicBlockIterator::operator++(void) {
  BasicBlock *curr(cursor->list.GetNext(cursor));
  BasicBlock *next(nullptr);

  // Auto-clean up blocks while iterating over them.
  for (; curr && curr->CanDestroy(); curr = next) {
    next = curr->list.GetNext(curr);
    curr->list.Unlink();
    delete curr;
  }

  cursor = curr;
}

// Get a basic block out of the iterator.
BasicBlock *BasicBlockIterator::operator*(void) const {
  return cursor;
}

}  // namespace granary
