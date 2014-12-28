/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_STACK_TRACE_CLIENT_H_
#define CLIENTS_STACK_TRACE_CLIENT_H_

#include <granary.h>

// Copy up to `buff_size` of the most recent program counters from the stack
// trace into `buff`, and return the number of copied
size_t CopyStackTrace(granary::AppPC *buff, size_t buff_size);

template <size_t kBuffSize>
inline static size_t CopyStackTrace(granary::AppPC (&trace)[kBuffSize]) {
  return CopyStackTrace(trace, kBuffSize);
}

#endif  // CLIENTS_STACK_TRACE_CLIENT_H_
