/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/breakpoint.h"
#include "granary/cfg/instruction.h"

namespace granary {

Instruction *Instruction::Next(void) {
  return list.GetNext(this);
}

Instruction *Instruction::Previous(void) {
  return list.GetPrevious(this);
}

void Instruction::InsertBefore(Instruction *that) {
  list.SetPrevious(this, that);
}

void Instruction::InsertAfter(Instruction *that) {
  list.SetNext(this, that);
}

void Instruction::Unlink(void) {
  list.Unlink();
}

void AnnotationInstruction::Unlink(void) {
  granary_break_on_fault();
}

}  // namespace granary
