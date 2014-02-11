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
class BlockFactory;

namespace driver {
GRANARY_INTERNAL_DEFINITION class DecodedInstruction;
}  // namespace driver

// Represents an abstract instruction.
class Instruction {
 public:

  GRANARY_INTERNAL_DEFINITION
  inline Instruction(void)
      : list() {}

  virtual ~Instruction(void) = default;

  GRANARY_DECLARE_BASE_CLASS(Instruction)

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

  // Used to put instructions into lists.
  GRANARY_INTERNAL_DEFINITION ListHead list;

 private:
  GRANARY_IF_EXTERNAL( Instruction(void) = delete; )
  GRANARY_DISALLOW_COPY_AND_ASSIGN(Instruction);
};

// Built-in annotations.
GRANARY_INTERNAL_DEFINITION
enum InstructionAnnotation {
  BEGIN_BASIC_BLOCK,
  END_BASIC_BLOCK,
  BEGIN_MIGHT_FAULT,
  END_MIGHT_FAULT,

  // Used to bound atomic regions of code.
  BEGIN_DELAY_INTERRUPT,
  END_DELAY_INTERRUPT,

  // Target of a branch instruction.
  LABEL,

  // When eliding function calls (for partial function inlining), we have a
  // special annotation that takes the place of the function call instruction
  // and is responsible for pushing the function's return address on the stack.
  PUSH_FUNCTION_RETURN_ADDRESS
};

// An annotation instruction is an environment-specific and implementation-
// specific annotations for basic blocks. Some examples would be that some
// instructions might result in page faults within kernel code. Annotations
// are used to mark those boundaries (e.g. by having an annotation that begins
// a faultable sequence of instructions and an annotation that ends it).
// Annotation instructions should not be removed by instrumentation.
class AnnotationInstruction final : public Instruction {
 public:
  virtual ~AnnotationInstruction(void) = default;

  GRANARY_INTERNAL_DEFINITION
  inline AnnotationInstruction(InstructionAnnotation annotation_,
                               const void *data_=nullptr)
      : annotation(annotation_),
        data(data_) {}

#ifdef GRANARY_DEBUG
  virtual Instruction *InsertBefore(std::unique_ptr<Instruction>);
  virtual Instruction *InsertAfter(std::unique_ptr<Instruction>);
#endif

  // Returns true if this instruction is a label.
  bool IsLabel(void) const;

  // Returns true if this instruction is targeted by any branches.
  bool IsBranchTarget(void) const;

  GRANARY_INTERNAL_DEFINITION const InstructionAnnotation annotation;
  GRANARY_INTERNAL_DEFINITION const void * const data;

  GRANARY_DECLARE_DERIVED_CLASS_OF(Instruction, AnnotationInstruction)
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

  GRANARY_DECLARE_DERIVED_CLASS_OF(Instruction, NativeInstruction)
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
class BranchInstruction final : public NativeInstruction {
 public:
  virtual ~BranchInstruction(void) = default;

  GRANARY_INTERNAL_DEFINITION
  inline BranchInstruction(driver::DecodedInstruction *instruction_,
                           const AnnotationInstruction *target_)
      : NativeInstruction(instruction_),
        target(target_) {}

  // Return the targeted instruction of this branch.
  const AnnotationInstruction *TargetInstruction(void) const;

  GRANARY_DECLARE_DERIVED_CLASS_OF(Instruction, BranchInstruction)
  GRANARY_DEFINE_NEW_ALLOCATOR(BranchInstruction, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  BranchInstruction(void) = delete;

  // Instruction targeted by this branch. Assumed to be within the same
  // basic block as this instruction.
  GRANARY_INTERNAL_DEFINITION const AnnotationInstruction * const target;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(BranchInstruction);
};

// Represents a control-flow instruction that is not local to a basic block,
// i.e. transfers control to another basic block.
//
// Note: A special case is that a non-local control-flow instruction can
//       redirect control back to the beginning of the basic block.
class ControlFlowInstruction final : public NativeInstruction {
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
  bool IsUnconditionalJump(void) const;
  bool IsConditionalJump(void) const;
  bool HasIndirectTarget(void) const;

  // Return the target block of this CFI.
  BasicBlock *TargetBlock(void) const;

  GRANARY_DECLARE_DERIVED_CLASS_OF(Instruction, ControlFlowInstruction)
  GRANARY_DEFINE_NEW_ALLOCATOR(ControlFlowInstruction, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  friend class BlockFactory;

  ControlFlowInstruction(void) = delete;

  // Target block of this CFI.
  GRANARY_INTERNAL_DEFINITION mutable BasicBlock *target;

  GRANARY_INTERNAL_DEFINITION
  void ChangeTarget(BasicBlock *new_target) const;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ControlFlowInstruction);
};

}  // namespace granary

#endif  // GRANARY_CFG_INSTRUCTION_H_
