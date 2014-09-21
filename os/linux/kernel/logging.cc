/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/lock.h"
#include "granary/base/string.h"

#include "os/logging.h"

// Visible from GDB.
extern "C" char granary_log_buffer[32768 << 5] = {'\0'};

namespace granary {
namespace os {

// Initialize the logging mechanism.
void InitLog(void) {}

namespace {
static SpinLock log_buffer_lock;
static unsigned long log_buffer_index = 0;
}

// Log something.
//
// TODO(pag): This is totally unsafe! It can easily overflow.
int Log(LogLevel, const char *format, ...) {
  SpinLockedRegion locker(&log_buffer_lock);
  va_list args;
  va_start(args, format);
  auto ret = 0UL;
  if (log_buffer_index < sizeof granary_log_buffer) {
    ret = VarFormat(&(granary_log_buffer[log_buffer_index]),
                    sizeof granary_log_buffer - log_buffer_index - 1,
                    format, args);
    log_buffer_index += ret;
  }
  va_end(args);
  return static_cast<int>(ret);
}

}  // namespace os
}  // namespace granary
