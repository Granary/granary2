/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"

namespace granary {

void InstructionIterator::operator++(void) {
  instr = instr->Next();
}

void ReverseInstructionIterator::operator++(void) {
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

ReverseAppInstructionIterator::ReverseAppInstructionIterator(
    Instruction *instr_)
    : instr(FindPreviousAppInstruction(instr_)) {}

void ReverseAppInstructionIterator::operator++(void) {
  instr = FindPreviousAppInstruction(instr->Previous());
}


// Move the iterator to the next basic block.
void BasicBlockIterator::operator++(void) {
  cursor = cursor->list.Next();
}

// Get a basic block out of the iterator.
BasicBlock *BasicBlockIterator::operator*(void) const {
  return cursor;
}


// Move the iterator to the previous basic block.
void ReverseBasicBlockIterator::operator++(void) {
  cursor = cursor->list.Previous();
}

// Get a basic block out of the iterator.
BasicBlock *ReverseBasicBlockIterator::operator*(void) const {
  return cursor;
}

}  // namespace granary
