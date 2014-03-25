/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_DRIVER_H_
#define GRANARY_DRIVER_DRIVER_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

// Include the generic driver interfaces that can be directly used.
#include "granary/arch/init.h"
#include "granary/arch/decode.h"
#include "granary/arch/encode.h"
#include "granary/arch/relativize.h"

// Include "documentation" driver interfaces (for example:
// `arch::InstructionInterface`and `arch::OperandInterface`). These serve
// only to document what methods are publicly available on driver classes, as
// well as what methods must be implemented by people porting Granary+ to a new
// architecture/driver.
#include "granary/arch/instruction.h"
#include "granary/arch/operand.h"

// Include specific driver code.
#include "granary/arch/x86-64/instruction.h"
#include "granary/arch/x86-64/operand.h"

#endif  // GRANARY_DRIVER_DRIVER_H_
