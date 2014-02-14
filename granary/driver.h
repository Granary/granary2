/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_H_
#define GRANARY_DRIVER_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/driver/dynamorio/decoder.h"
#include "granary/driver/dynamorio/instruction.h"
#include "granary/driver/dynamorio/relativize.h"

namespace granary {

// Forward declarations.
class NativeInstruction;

namespace driver {

// Initialize the driver (instruction encoder/decoder).
void Init(void);

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_H_
