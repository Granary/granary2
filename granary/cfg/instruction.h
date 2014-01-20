/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_CFG_INSTRUCTION_H_
#define GRANARY_CFG_INSTRUCTION_H_

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/list.h"

namespace granary {

// Forward declarations.
class BasicBlock;
class ControlFlowInstruction;

namespace driver {
class DecodedInstruction;
}  // namespace driver

GRANARY_DECLARE_CLASS_HEIRARCHY(
    Instruction,
    LabelInstruction,
    AnnotationInstruction,
    NativeInstruction,
    ControlFlowInstruction,
    LocalControlFlowInstruction,
    NonLocalControlFlowInstruction);

// Represents an abstract instruction.
class Instruction {
 public:
  Instruction(void) = default;
  virtual ~Instruction(void) = default;

  GRANARY_BASE_CLASS(Instruction)

  Instruction *Next(void);
  Instruction *Previous(void);

  void InsertBefore(Instruction *);
  void InsertAfter(Instruction *);

  // Unlink an instruction from an instruction list. Care must be taken to
  // ensure that the instruction is deleted.
  virtual void Unlink(void);

 private:
  ListHead list;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Instruction);
};

// A label instruction is the target of a local control-flow instruction.
class LabelInstruction : public Instruction {
 public:
  virtual ~LabelInstruction(void) = default;

  GRANARY_DERIVED_CLASS_OF(Instruction, LabelInstruction)

 private:
  LabelInstruction(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(LabelInstruction);
};

// An annotation instruction is an environment-specific and implementation-
// specific annotations for basic blocks. Some examples would be that some
// instructions might result in page faults within kernel code. Annotations
// are used to mark those boundaries (e.g. by having an annotation that begins
// a faultable sequence of instructions and an annotation that ends it).
// Annotation instructions should not be removed by instrumentation.
class AnnotationInstruction : public Instruction {
 public:
  virtual ~AnnotationInstruction(void) = default;

  GRANARY_DERIVED_CLASS_OF(Instruction, AnnotationInstruction)

  // Re-implement to disable unlinking from an instruction list.
  virtual void Unlink(void);

 private:
  AnnotationInstruction(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(AnnotationInstruction);
};

// An instruction containing an driver-specific decoded instruction.
class NativeInstruction : public Instruction {
 public:
  virtual ~NativeInstruction(void) = default;

  GRANARY_DERIVED_CLASS_OF(Instruction, NativeInstruction)

 private:
  friend class ControlFlowInstruction;

  std::unique_ptr<driver::DecodedInstruction> instruction;
};

// Represents a control-flow instruction.
class ControlFlowInstruction : public NativeInstruction {
 public:
  virtual ~ControlFlowInstruction(void) = default;
  explicit ControlFlowInstruction(driver::DecodedInstruction *instruction_);

  GRANARY_DERIVED_CLASS_OF(Instruction, ControlFlowInstruction)

  // Driver-specific implementations.
  bool IsFunctionCall(void) const;
  bool IsFunctionReturn(void) const;
  bool IsInterruptReturn(void) const;
  bool IsJump(void) const;
  bool IsConditionalJump(void) const;
  bool HasIndirectTarget(void) const;

 private:
  ControlFlowInstruction(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ControlFlowInstruction);
};

// Represents a control-flow instruction that is local to a basic block, i.e.
// keeps control within the same basic block.
class LocalControlFlowInstruction : public ControlFlowInstruction {
 public:
  LocalControlFlowInstruction(driver::DecodedInstruction *instruction_,
                              const LabelInstruction *target_);

  virtual ~LocalControlFlowInstruction(void) = default;

  GRANARY_DERIVED_CLASS_OF(Instruction, LocalControlFlowInstruction)

  LabelInstruction * const target;

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
  NonLocalControlFlowInstruction(driver::DecodedInstruction *instruction_,
                                 BasicBlock *target_);

  virtual ~NonLocalControlFlowInstruction(void) = default;

  GRANARY_DERIVED_CLASS_OF(Instruction, NonLocalControlFlowInstruction)

  BasicBlock * const target;

 private:
  NonLocalControlFlowInstruction(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(NonLocalControlFlowInstruction);
};

}  // namespace granary

#endif  // GRANARY_CFG_INSTRUCTION_H_
