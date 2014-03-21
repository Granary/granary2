/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_DRIVER_OPERAND_H_
#define GRANARY_DRIVER_OPERAND_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/cfg/operand.h"

namespace granary {
namespace driver {

// Documents the methods that must be provided by driver operands. This
// interface class cannot be used as-is as the methods don't exist.
class OperandInterface {
 public:
  bool IsRead(void) const;

  bool IsWrite(void) const;

  bool IsConditionalRead(void) const;

  bool IsConditionalWrite(void) const;

  bool IsRegister(void) const;
  bool IsMemory(void) const;
  bool IsImmediate(void) const;

  void EncodeToString(OperandString *str) const;
};

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_OPERAND_H_
