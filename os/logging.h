/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_LOGGING_H_
#define GRANARY_LOGGING_H_

namespace granary {
namespace os {

enum LogLevel : int {
  LogOutput = 0,
  LogWarning = 1,
  LogError = 2,
  LogFatalError = 3,
  LogDebug = 4
};

int Log(LogLevel, const char *, ...) __attribute__ ((format (printf, 2, 3)));

}  // namespace os
}  // namespace granary

#endif  // GRANARY_LOGGING_H_
