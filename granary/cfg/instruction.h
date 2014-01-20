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

  // Inserts an instruction before/after the current instruction. Returns an
  // (unowned) pointer to the inserted instruction.
  Instruction *InsertBefore(std::unique_ptr<Instruction>);
  Instruction *InsertAfter(std::unique_ptr<Instruction>);

  // Unlink an instruction from an instruction list.
  static std::unique_ptr<Instruction> Unlink(Instruction *);

 private:
  ListHead list;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Instruction);
};

// Implemented in `granary/decoder.cc`.
enum InstructionAnnotation : uint32_t;

// An annotation instruction is an environment-specific and implementation-
// specific annotations for basic blocks. Some examples would be that some
// instructions might result in page faults within kernel code. Annotations
// are used to mark those boundaries (e.g. by having an annotation that begins
// a faultable sequence of instructions and an annotation that ends it).
// Annotation instructions should not be removed by instrumentation.
class AnnotationInstruction : public Instruction {
 public:
  AnnotationInstruction(InstructionAnnotation annotation_, void *data_=nullptr);
  virtual ~AnnotationInstruction(void) = default;

  GRANARY_DERIVED_CLASS_OF(Instruction, AnnotationInstruction)

 private:
  AnnotationInstruction(void) = delete;

  const InstructionAnnotation annotation;
  void * const data;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(AnnotationInstruction);
};

// An instruction containing an driver-specific decoded instruction.
class NativeInstruction : public Instruction {
 public:
  explicit NativeInstruction(driver::DecodedInstruction *instruction_);
  virtual ~NativeInstruction(void) = default;

  GRANARY_DERIVED_CLASS_OF(Instruction, NativeInstruction)

 private:
  friend class ControlFlowInstruction;

  NativeInstruction(void) = delete;

  // TODO(pag): In future we could potentially put `driver::DecodedInstruction`
  //            by value instead of by pointer, but that could potentially
  //            get ugly.
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
                              const AnnotationInstruction *target_);

  virtual ~LocalControlFlowInstruction(void) = default;

  GRANARY_DERIVED_CLASS_OF(Instruction, LocalControlFlowInstruction)

  AnnotationInstruction * const target;

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
