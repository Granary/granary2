/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/instrument.h"

namespace granary {
namespace detail {

// Take over a program's execution by replacing a return address with an
// instrumented return address.
void Instrument(AppProgramCounter *return_address) {
  GRANARY_UNUSED(return_address);
}

}  // namespace detail
}  // namespace granary
