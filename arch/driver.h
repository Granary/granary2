/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_DRIVER_H_
#define GRANARY_ARCH_DRIVER_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

// Include the generic driver interfaces that can be directly used.
#include "arch/init.h"
#include "arch/decode.h"
#include "arch/encode.h"

// Include "documentation" driver interfaces (for example:
// `arch::InstructionInterface`and `arch::OperandInterface`). These serve
// only to document what methods are publicly available on driver classes, as
// well as what methods must be implemented by people porting Granary+ to a new
// architecture/driver.
#include "arch/instruction.h"
#include "arch/operand.h"

// Include specific driver code.
#include "arch/x86-64/instruction.h"
#include "arch/x86-64/operand.h"

#endif  // GRANARY_ARCH_DRIVER_H_
