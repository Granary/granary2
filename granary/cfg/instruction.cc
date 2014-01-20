/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/breakpoint.h"
#include "granary/cfg/instruction.h"
#include "granary/driver/instruction.h"

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

AnnotationInstruction::AnnotationInstruction(InstructionAnnotation annotation_,
                                             void *data_)
    : annotation(annotation_),
      data(data_) {}

NativeInstruction::NativeInstruction(driver::DecodedInstruction *instruction_)
    : instruction(instruction_) {}

}  // namespace granary
