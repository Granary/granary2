/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/context.h"

#include "os/logging.h"
#include "os/memory.h"
#include "os/module.h"

namespace granary {
extern "C" {

// Exported to assembly code. This is the "fast" version of Granary's exit,
// where almost all resources are *not* cleaned up.
void granary_exit(ExitReason reason) {
  GlobalContext()->ExitTools(reason);
  os::ExitLog();
}

// Exported to assembly code. Performs a full Granary exit. This version needs
// to perform full cleanup procedures.
void granary_exit_slow(ExitReason reason) {
  GlobalContext()->ExitTools(reason);
  ExitContext();
  os::ExitLog();
  os::ExitModuleManager();
  os::ExitHeap();
}

}  // extern C
}  // namespace granary
