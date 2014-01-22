/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_DRIVER_H_
#define GRANARY_DRIVER_H_

#ifdef GRANARY_INTERNAL
#include "granary/driver/dynamorio/decoder.h"
#include "granary/driver/dynamorio/instruction.h"

namespace granary {
namespace driver {

void Init(void);

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_INTERNAL
#endif  // GRANARY_DRIVER_H_
