/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LOGGING_H_
#define OS_LOGGING_H_

namespace granary {
namespace os {

enum LogLevel : int {
  LogOutput = 0,
  LogDebug = 1
};

// Initialize the logging mechanism.
GRANARY_INTERNAL_DEFINITION void InitLog(void);

// Exit the log.
GRANARY_INTERNAL_DEFINITION void ExitLog(void);

// Log something.
int Log(LogLevel, const char *, ...) __attribute__ ((format (printf, 2, 3)));

}  // namespace os
}  // namespace granary

#endif  // OS_LOGGING_H_
