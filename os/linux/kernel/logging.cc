/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/lock.h"
#include "granary/base/string.h"

#include "os/logging.h"

// Visible from GDB.
extern "C" {
char granary_log_buffer[32768 << 5] = {'\0'};
unsigned long granary_log_buffer_index = 0;
}  // extern C

namespace granary {
namespace os {

// Initialize the logging mechanism.
void InitLog(void) {}

// Exit the log.
void ExitLog(void) {}

namespace {
static SpinLock log_buffer_lock;
}

// Log something.
//
// TODO(pag): This is totally unsafe! It can easily overflow.
int Log(LogLevel, const char *format, ...) {
  SpinLockedRegion locker(&log_buffer_lock);
  va_list args;
  va_start(args, format);
  auto ret = 0UL;
  if (granary_log_buffer_index < sizeof granary_log_buffer) {
    ret = VarFormat(&(granary_log_buffer[granary_log_buffer_index]),
                    sizeof granary_log_buffer - granary_log_buffer_index - 1,
                    format, args);
    granary_log_buffer_index += ret;
  }
  va_end(args);
  return static_cast<int>(ret);
}

}  // namespace os
}  // namespace granary
