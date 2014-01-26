/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_INSTRUCTION_H_
#define GRANARY_CFG_INSTRUCTION_H_

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/list.h"
#include "granary/base/new.h"

namespace granary {

// Forward declarations.
class BasicBlock;
class ControlFlowInstruction;

namespace driver {
class DecodedInstruction;
}  // namespace driver

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (Instruction, 2),
    (AnnotationInstruction, 2 * 3),
    (NativeInstruction, 2 * 5),
    (BranchInstruction, 2 * 5 * 7),
    (ControlFlowInstruction, 2 * 5 * 11))

// Represents an abstract instruction.
class Instruction {
 public:
  inline Instruction(void)
      : list() {}

  virtual ~Instruction(void) = default;

  GRANARY_BASE_CLASS(Instruction)

  Instruction *Next(void);
  Instruction *Previous(void);
  virtual int Length(void) const;

  // Inserts an instruction before/after the current instruction. Returns an
  // (unowned) pointer to the inserted instruction.
  GRANARY_IF_DEBUG(virtual)
  Instruction *InsertBefore(std::unique_ptr<Instruction>);

  GRANARY_IF_DEBUG(virtual)
  Instruction *InsertAfter(std::unique_ptr<Instruction>);

  // Unlink an instruction from an instruction list.
  static std::unique_ptr<Instruction> Unlink(Instruction *);

 GRANARY_PROTECTED:
  ListHead list;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(Instruction);
};

// Built-in annotations.
enum InstructionAnnotation {
  BEGIN_BASIC_BLOCK,
  END_BASIC_BLOCK,
  BEGIN_MIGHT_FAULT,
  END_MIGHT_FAULT,
  BEGIN_DELAY_INTERRUPT,
  END_DELAY_INTERRUPT,
  LABEL
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

  GRANARY_INTERNAL_DEFINITION
  inline AnnotationInstruction(InstructionAnnotation annotation_,
                               void *data_=nullptr)
      : annotation(annotation_),
        data(data_) {}

#ifdef GRANARY_DEBUG
  virtual Instruction *InsertBefore(std::unique_ptr<Instruction>);
  virtual Instruction *InsertAfter(std::unique_ptr<Instruction>);
#endif

  const InstructionAnnotation annotation;
  void * const data;

  GRANARY_DERIVED_CLASS_OF(Instruction, AnnotationInstruction)
  GRANARY_DEFINE_NEW_ALLOCATOR(AnnotationInstruction, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  AnnotationInstruction(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(AnnotationInstruction);
};

// An instruction containing an driver-specific decoded instruction.
class NativeInstruction : public Instruction {
 public:
  virtual ~NativeInstruction(void);

  GRANARY_INTERNAL_DEFINITION
  explicit NativeInstruction(driver::DecodedInstruction *instruction_);

  virtual int Length(void) const;

  GRANARY_DERIVED_CLASS_OF(Instruction, NativeInstruction)
  GRANARY_DEFINE_NEW_ALLOCATOR(AnnotationInstruction, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  friend class ControlFlowInstruction;

  NativeInstruction(void) = delete;

  GRANARY_INTERNAL_DEFINITION
  std::unique_ptr<driver::DecodedInstruction> instruction;
};

// Represents a control-flow instruction that is local to a basic block, i.e.
// keeps control within the same basic block.
class BranchInstruction : public NativeInstruction {
 public:
  virtual ~BranchInstruction(void) = default;

  GRANARY_INTERNAL_DEFINITION
  inline BranchInstruction(driver::DecodedInstruction *instruction_,
                           const AnnotationInstruction *target_)
      : NativeInstruction(instruction_),
        target(target_) {}

  const AnnotationInstruction * const target;

  GRANARY_DERIVED_CLASS_OF(Instruction, BranchInstruction)
  GRANARY_DEFINE_NEW_ALLOCATOR(BranchInstruction, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  BranchInstruction(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(BranchInstruction);
};

// Represents a control-flow instruction that is not local to a basic block,
// i.e. transfers control to another basic block.
//
// Note: A special case is that a non-local control-flow instruction can
//       redirect control back to the beginning of the basic block.
class ControlFlowInstruction : public NativeInstruction {
 public:
  virtual ~ControlFlowInstruction(void);

  GRANARY_INTERNAL_DEFINITION
  ControlFlowInstruction(driver::DecodedInstruction *instruction_,
                         BasicBlock *target_);

  // Driver-specific implementations.
  bool IsFunctionCall(void) const;
  bool IsFunctionReturn(void) const;
  bool IsInterruptCall(void) const;
  bool IsInterruptReturn(void) const;
  bool IsSystemCall(void) const;
  bool IsSystemReturn(void) const;
  bool IsJump(void) const;
  bool IsConditionalJump(void) const;
  bool HasIndirectTarget(void) const;

  inline BasicBlock *TargetBlock(void) const {
    return target;
  }

  GRANARY_DERIVED_CLASS_OF(Instruction, ControlFlowInstruction)
  GRANARY_DEFINE_NEW_ALLOCATOR(ControlFlowInstruction, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  friend class ControlFlowGraph;

  ControlFlowInstruction(void) = delete;

  mutable BasicBlock *target;

  GRANARY_INTERNAL_DEFINITION
  void ChangeTarget(BasicBlock *new_target) const;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ControlFlowInstruction);
};

}  // namespace granary

#endif  // GRANARY_CFG_INSTRUCTION_H_
