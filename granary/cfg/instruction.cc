/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_IMPLEMENT_DYNAMIC_CAST

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/breakpoint.h"
#include "granary/driver.h"

namespace granary {

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (Instruction, 2),
    (AnnotationInstruction, 2 * 3),
    (NativeInstruction, 2 * 5),
    (BranchInstruction, 2 * 5 * 7),
    (ControlFlowInstruction, 2 * 5 * 11))

GRANARY_DEFINE_BASE_CLASS(Instruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, AnnotationInstruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, NativeInstruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, BranchInstruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, ControlFlowInstruction)

Instruction *Instruction::Next(void) {
  return list.GetNext(this);
}

Instruction *Instruction::Previous(void) {
  return list.GetPrevious(this);
}

// By default, non-native instructions are treated as having zero length.
int Instruction::Length(void) const {
  return 0;
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
  granary_break_on_fault_if(
      GRANARY_UNLIKELY(IsA<AnnotationInstruction *>(instr)));
  instr->list.Unlink();
  return std::unique_ptr<Instruction>(instr);
}

#ifdef GRANARY_DEBUG
// Prevent adding an instruction before the beginning instruction of a basic
// block.
Instruction *AnnotationInstruction::InsertBefore(
    std::unique_ptr<Instruction> that) {
  granary_break_on_fault_if(GRANARY_UNLIKELY(BEGIN_BASIC_BLOCK == annotation));
  return this->Instruction::InsertBefore(std::move(that));
}

// Prevent adding an instruction after the ending instruction of a basic block.
Instruction *AnnotationInstruction::InsertAfter(
    std::unique_ptr<Instruction> that) {
  granary_break_on_fault_if(GRANARY_UNLIKELY(END_BASIC_BLOCK == annotation));
  return this->Instruction::InsertAfter(std::move(that));
}
#endif  // GRANARY_DEBUG

NativeInstruction::NativeInstruction(driver::DecodedInstruction *instruction_)
    : instruction(instruction_) {}

NativeInstruction::~NativeInstruction(void) {}

// Get the length of the instruction.
int NativeInstruction::Length(void) const {
  return instruction->Length();
}

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
  auto old_target = target;
  target = nullptr;

  // In some cases, instructions need to clean up after basic blocks. E.g.
  // a CTI is unlinked, never re-linked, and therefore goes out of scope, thus
  // deleting the instruction. If that CTI is the only link to a basic block,
  // then the associated block must also be destroyed.
  if (!old_target->list && old_target->CanDestroy()) {
    delete old_target;
  }
}

// Change the target of a control-flow instruction. This can involve an
// ownership transfer of the targeted basic block.
void ControlFlowInstruction::ChangeTarget(BasicBlock *new_target) const {
  auto old_target = target;
  new_target->Acquire();
  target = new_target;
  old_target->Release();
}

}  // namespace granary
