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

namespace {
// Returns the next application instruction, given an arbitrary instruction. If
// the given instruction is an application instruction then it will be returned.
static NativeInstruction *FindNextAppInstruction(Instruction *instr) {
  for (; instr; instr = instr->Next()) {
    auto native_instr = DynamicCast<NativeInstruction *>(instr);
    if (native_instr && native_instr->IsAppInstruction()) {
      return native_instr;
    }
  }
  return nullptr;
}

// Returns the previous application instruction, given an arbitrary instruction.
// If the given instruction is an application instruction then it will be
// returned.
static NativeInstruction *FindPreviousAppInstruction(Instruction *instr) {
  for (; instr; instr = instr->Previous()) {
    auto native_instr = DynamicCast<NativeInstruction *>(instr);
    if (native_instr && native_instr->IsAppInstruction()) {
      return native_instr;
    }
  }
  return nullptr;
}
}  // namespace

AppInstructionIterator::AppInstructionIterator(Instruction *instr_)
    : instr(FindNextAppInstruction(instr_)) {}

void AppInstructionIterator::operator++(void) {
  instr = FindNextAppInstruction(instr->Next());
}

BackwardAppInstructionIterator::BackwardAppInstructionIterator(
    Instruction *instr_)
    : instr(FindPreviousAppInstruction(instr_)) {}

void BackwardAppInstructionIterator::operator++(void) {
  instr = FindPreviousAppInstruction(instr->Previous());
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
