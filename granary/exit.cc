/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/context.h"

namespace granary {
extern "C" {

// Exported to assembly code.
void granary_exit(ExitReason reason) {
  GlobalContext()->ExitTools(reason);
}

}  // extern C
}  // namespace granary
