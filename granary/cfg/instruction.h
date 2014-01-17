/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_CFG_INSTRUCTION_H_
#define GRANARY_CFG_INSTRUCTION_H_

#include "granary/base/base.h"
#include "granary/base/cast.h"

namespace granary {
namespace driver {
class DecodedInstruction;
}  // namespace driver

class BasicBlock;

GRANARY_DECLARE_CLASS_HEIRARCHY(
    Instruction,
    LabelInstruction,
    ControlFlowInstruction);

// Represents an abstract instruction.
class Instruction {
 public:
  virtual ~Instruction(void) = default;

  Instruction *next;
  Instruction *prev;
  BasicBlock *block;

  GRANARY_BASE_CLASS(Instruction)

 private:
  Instruction(void) = delete;

  driver::DecodedInstruction *instruction;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Instruction);
};

// Represents a label instruction. A label instruction is the target of a
// control-flow instruction.
class LabelInstruction : public Instruction {
 public:
  virtual ~LabelInstruction(void) = delete;

  GRANARY_DERIVED_CLASS_OF(Instruction, LabelInstruction)

 private:
  LabelInstruction(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(LabelInstruction);
};

// Represents a control-flow instruction.
class ControlFlowInstruction : public Instruction {
 public:
  GRANARY_DERIVED_CLASS_OF(Instruction, ControlFlowInstruction)

 private:
  ControlFlowInstruction(void) = delete;
  GRANARY_DISALLOW_COPY_AND_ASSIGN(ControlFlowInstruction);
};

// Represents a control-flow instruction that is local to a basic block, i.e.
// keeps control within the same basic block.
class LocalControlFlowInstruction : public ControlFlowInstruction {
 public:
  GRANARY_DERIVED_CLASS_OF(Instruction, LocalControlFlowInstruction)

 private:
  LocalControlFlowInstruction(void) = delete;
  GRANARY_DISALLOW_COPY_AND_ASSIGN(LocalControlFlowInstruction);
};

// Represents a control-flow instruction that is not local to a basic block,
// i.e. transfers control to another basic block.
//
// Note: A special case is that a non-local control-flow instruction can
//       redirect control back to the beginning of the basic block.
class NonLocalControlFlowInstruction : public ControlFlowInstruction {
 public:
  GRANARY_DERIVED_CLASS_OF(Instruction, NonLocalControlFlowInstruction)

 private:
  NonLocalControlFlowInstruction(void) = delete;
  GRANARY_DISALLOW_COPY_AND_ASSIGN(NonLocalControlFlowInstruction);
};

}  // namespace granary

#endif  // GRANARY_CFG_INSTRUCTION_H_
