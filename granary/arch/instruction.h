/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_INSTRUCTION_H_
#define GRANARY_ARCH_INSTRUCTION_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/cfg/operand.h"

#include "granary/base/base.h"
#include "granary/base/pc.h"

namespace granary {
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

  // Returns true if an instruction reads the flags.
  bool ReadsFlags(void) const;

  // Returns true if an instruction writes to the flags.
  bool WritesFlags(void) const;

  const char *OpCodeName(void) const;

  // Apply a function to every operand.
  void ForEachOperand(const std::function<void(granary::Operand *)> &func);

  // Operand matcher for multiple arguments. Returns the number of matched
  // arguments, starting from the first argument.
  size_t CountMatchedOperands(std::initializer_list<OperandMatcher> &&matchers);
};

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_ARCH_INSTRUCTION_H_