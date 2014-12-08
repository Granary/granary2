/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_INIT_H_
#define GRANARY_INIT_H_

#include "granary/base/base.h"

namespace granary {

enum InitReason {
  // This says we're initializing Granary before a running program has begun.
  // In user space, this is followed up by a `kInitThread` for the main
  // program thread.
  kInitProgram = 0,

  // `kInitAttach` and `kInitThread` are similar. `kInitAttach` says that we're
  // attaching to an existing program, and therefore an existing thread. So,
  // if `kInitThread` triggers something, then `kInitAttach` should also trigger
  // the same behavior.
  kInitAttach = 1,
  kInitThread = 2,

  // This says we're initializing Granary to run a test case.
  kInitTestCase
};

// Runs the constructors from the initialization array.
GRANARY_INTERNAL_DEFINITION void PreInit(void);

// Initializes Granary.
GRANARY_INTERNAL_DEFINITION void Init(InitReason reason);

}  // namespace granary

#endif  // GRANARY_INIT_H_
