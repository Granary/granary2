/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "os/thread.h"

#include "arch/base.h"

namespace granary {
namespace os {

// Notify Granary tools that a thread has been created.
void InitThread(void) {
  InitTools(kInitThread);

  // TODO(pag): ????
}

// Notify Granary tools that a thread has been destroyed.
void ExitThread(void) {
  ExitTools(kExitThread);

  // TODO(pag): ????
}

// Yield the thread.
void YieldThread(void) {
  arch::Relax();
}

}  // namespace os
}  // namespace granary
