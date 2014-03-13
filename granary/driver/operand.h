/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_DRIVER_OPERAND_H_
#define GRANARY_DRIVER_OPERAND_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/code/operand.h"

namespace granary {
namespace driver {

// Documents the methods that must be provided by driver operands. This
// interface class cannot be used as-is as the methods don't exist.
class OperandInterface {
 public:
  inline bool IsRead(void) const;

  inline bool IsWrite(void) const;

  inline bool IsConditionalRead(void) const;

  inline bool IsConditionalWrite(void) const;

  void EncodeToString(OperandString *str) const;
};

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_OPERAND_H_
