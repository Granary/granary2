/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_INSTRUCTION_H_
#define GRANARY_DRIVER_INSTRUCTION_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/base.h"
#include "granary/base/pc.h"

namespace granary {

// Forward declarations.
class Operand;

namespace driver {

// Documents the methods that must be provided by driver instructions. This
// interface class cannot be used as-is as the methods don't exist.
class InstructionInterface {
 public:

  int DecodedLength(void) const;

  PC DecodedPC(void) const;

  PC BranchTarget(void) const;

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

  const char *OpCodeName(void) const;

  void ForEachOperand(std::function<void(granary::Operand *)> func);
};

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_INSTRUCTION_H_
