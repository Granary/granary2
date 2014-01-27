/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_INSTRUMENT_H_
#define GRANARY_INSTRUMENT_H_

#include "granary/base/types.h"

namespace granary {
namespace detail {

// Take over a program's execution by replacing a return address with an
// instrumented return address.
void Instrument(AppProgramCounter *return_address);

}  // namespace detail

// Begin instrumenting code.
extern void Instrument(void);

}  // namespace granary

#endif  // GRANARY_INSTRUMENT_H_
