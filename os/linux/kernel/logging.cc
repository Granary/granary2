/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "os/logging.h"

namespace granary {
namespace os {

// Initialize the logging mechanism.
void InitLog(void) {}

// Log something.
int Log(LogLevel, const char *, ...) {
  return 0;  // TODO(pag): Implement me.
}

}  // namespace os
}  // namespace granary
