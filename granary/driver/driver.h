/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_DRIVER_H_
#define GRANARY_DRIVER_DRIVER_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

// Include the generic driver interfaces that can be directly used.
#include "granary/driver/init.h"
#include "granary/driver/decode.h"
#include "granary/driver/encode.h"
#include "granary/driver/relativize.h"

// Include "documentation" driver interfaces (for example:
// `driver::InstructionInterface`and `driver::OperandInterface`). These serve
// only to document what methods are publicly available on driver classes, as
// well as what methods must be implemented by people porting Granary+ to a new
// architecture/driver.
#include "granary/driver/instruction.h"
#include "granary/driver/operand.h"

// Include specific driver code.
#include "granary/driver/xed2-intel64/instruction.h"
#include "granary/driver/xed2-intel64/operand.h"

#endif  // GRANARY_DRIVER_DRIVER_H_
