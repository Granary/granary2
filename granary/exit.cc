/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/context.h"
#include "os/logging.h"

namespace granary {
extern "C" {

// Exported to assembly code.
void granary_exit(ExitReason reason) {
  GlobalContext()->ExitTools(reason);
  os::ExitLog();
}

}  // extern C
}  // namespace granary
