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

// Declare that there is a strict, single-inheritance class hierarchy that
// uses virtual inheritance, so that we can apply `DynamicCast` to pointers to
// these types.
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
  GRANARY_DISALLOW_COPY_AND_ASSIGN(LabelInstruction);
};

// Represents a decoded control-flow instruction.
class ControlFlowInstruction : public Instruction {
 public:
  GRANARY_DERIVED_CLASS_OF(Instruction, ControlFlowInstruction)

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(ControlFlowInstruction);
};

}  // namespace granary

#endif  // GRANARY_CFG_INSTRUCTION_H_
