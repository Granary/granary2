/* Copyright 2014 Peter Goodman, all rights reserved. */

// TODO(pag): Issue #1: Refactor this code to use an output stream.

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "os/logging.h"

namespace granary {
namespace os {

// Log something.
int Log(LogLevel, const char *, ...) {
  return 0;  // TODO(pag): Implement me.
}

}  // namespace os
}  // namespace granary
