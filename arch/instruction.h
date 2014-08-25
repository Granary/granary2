/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef ARCH_INSTRUCTION_H_
#define ARCH_INSTRUCTION_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/cfg/operand.h"

#include "granary/base/base.h"
#include "granary/base/pc.h"

namespace granary {

// Forward declarations.
class AnnotationInstruction;

namespace arch {

// Documents the methods that must be provided by driver instructions. This
// interface class cannot be used as-is as the methods don't exist.
class InstructionInterface {
 public:

  int DecodedLength(void) const;

  PC DecodedPC(void) const;
  void SetDecodedPC(PC decoded_pc_);

  PC BranchTargetPC(void) const;

  // Invoke a function on the branch target, where the branch target is treated
  // as a `granary::Operand`.
  void WithBranchTargetOperand(
      const std::function<void(granary::Operand *)> &func);

  void SetBranchTarget(PC pc);

  // Set a branch target to be an annotation instruction.
  void SetBranchTarget(AnnotationInstruction *instr);

  bool IsFunctionCall(void) const;

  bool IsFunctionReturn(void) const;

  bool IsInterruptCall(void) const;

  bool IsInterruptReturn(void) const;

  bool IsSystemCall(void) const;

  bool IsSystemReturn(void) const;

  bool IsConditionalJump(void) const;

  bool IsUnconditionalJump(void) const;

  bool IsJump(void) const;

  // Returns true if this instruction is a control-flow instruction with an
  // indirect target.
  bool HasIndirectTarget(void) const;

  bool IsNoOp(void) const;

  // Returns true if an instruction reads from the stack pointer.
  bool ReadsFromStackPointer(void) const;

  // Returns true if an instruction writes to the stack pointer.
  bool WritesToStackPointer(void) const;

  // Returns true if the instruction modifies the stack pointer by a constant
  // value, otherwise returns false.
  bool ShiftsStackPointer(void) const;

  // Returns the statically know amount by which an instruction shifts the
  // stack pointer.
  //
  // Note: This should only be used after early mangling.
  int StackPointerShiftAmount(void) const;

  // If this instruction computes an address that is below (or possibly below)
  // the current stack pointer, then this function returns an estimate on that
  // amount. The value returned is either negative or zero.
  //
  // Note: This should only be used after early mangling.
  //
  // Note: If a dynamic offset is computed (e.g. stack pointer + register), then
  //       an ABI-specific value is returned. For example, for OSes running on
  //       x86-64/amd64 architectures, the user space red zone amount (-128) is
  //       returned, regardless of if Granary+ is instrumenting user space or
  //       kernel code.
  int ComputedOffsetBelowStackPointer(void) const;

  // Returns true if an instruction reads the flags.
  bool ReadsFlags(void) const;

  // Returns true if an instruction writes to the flags.
  bool WritesFlags(void) const;

  // Is this a specially inserted virtual register save or restore instruction?
  bool IsVirtualRegSaveRestore(void) const;

  const char *OpCodeName(void) const;
  const char *ISelName(void) const;

  // Apply a function to every operand.
  void ForEachOperand(const std::function<void(granary::Operand *)> &func);

  // Operand matcher for multiple arguments. Returns the number of matched
  // arguments, starting from the first argument.
  size_t CountMatchedOperands(std::initializer_list<OperandMatcher> &&matchers);

  // Does this instruction enable interrupts?
  bool EnablesInterrupts(void) const;

  // Does this instruction disable interrupts?
  bool DisablesInterrupts(void) const;

  // Can this instruction change the interrupt status to either of enabled or
  // disabled?
  bool CanEnableOrDisableInterrupts(void) const;
};

}  // namespace arch
}  // namespace granary

#endif  // ARCH_INSTRUCTION_H_
