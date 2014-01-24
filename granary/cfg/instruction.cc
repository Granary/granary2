/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/basic_block.h"
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

// Initialize a control-flow transfer instruction.
ControlFlowInstruction::ControlFlowInstruction(
    driver::DecodedInstruction *instruction_, BasicBlock *target_)
      : NativeInstruction(instruction_),
        target(target_) {
  target->Acquire();
}

// Destroy a control-flow transfer instruction.
ControlFlowInstruction::~ControlFlowInstruction(void) {
  target->Release();

  // In some cases, instructions need to clean up after basic blocks. E.g.
  // a CTI is unlinked, never re-linked, and therefore goes out of scope, thus
  // deleting the destructor. If that CTI is the only link to a basic block,
  // then the associated block must also be destroyed.
  //
  // This can cause a bit of thrashing when control-flow graphs are destroyed.
  // TODO(pag): Check that all behavior works out in this case.
  if (target->CanDestroy()) {

    // If it's in a basic block list, then the CFG will clean it up. Otherwise,
    // it's unknown and so the CTI cleans it up.
    if (!target->list) {
      delete target;
    }
  }

  target = nullptr;
}

}  // namespace granary
