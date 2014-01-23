/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/instruction.h"

#include "granary/breakpoint.h"
#include "granary/driver.h"

namespace granary {

Instruction *Instruction::Next(void) {
  return list.GetNext(this);
}

Instruction *Instruction::Previous(void) {
  return list.GetPrevious(this);
}

Instruction *Instruction::InsertBefore(std::unique_ptr<Instruction> that) {
  Instruction *instr(that.release());
  list.SetPrevious(this, instr);
  return instr;
}

Instruction *Instruction::InsertAfter(std::unique_ptr<Instruction> that) {
  Instruction *instr(that.release());
  list.SetNext(this, instr);
  return instr;
}

std::unique_ptr<Instruction> Instruction::Unlink(Instruction *instr) {
  if (GRANARY_UNLIKELY(IsA<AnnotationInstruction *>(instr))) {
    granary_break_on_fault();
  }

  instr->list.Unlink();
  return std::unique_ptr<Instruction>(instr);
}

#ifdef GRANARY_DEBUG
// Prevent adding an instruction before the beginning instruction of a basic
// block.
Instruction *AnnotationInstruction::InsertBefore(
    std::unique_ptr<Instruction> that) {
  if (GRANARY_UNLIKELY(BEGIN_BASIC_BLOCK == annotation)) {
    granary_break_on_fault();
  }
  return this->Instruction::InsertBefore(std::move(that));
}

// Prevent adding an instruction after the ending instruction of a basic block.
Instruction *AnnotationInstruction::InsertAfter(
    std::unique_ptr<Instruction> that) {
  if (GRANARY_UNLIKELY(END_BASIC_BLOCK == annotation)) {
    granary_break_on_fault();
  }
  return this->Instruction::InsertAfter(std::move(that));
}
#endif  // GRANARY_DEBUG

NativeInstruction::NativeInstruction(driver::DecodedInstruction *instruction_)
    : instruction(instruction_) {}

NativeInstruction::~NativeInstruction(void) {}

}  // namespace granary
