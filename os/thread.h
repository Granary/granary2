/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_THREAD_H_
#define OS_THREAD_H_

#include "granary/base/base.h"

namespace granary {
namespace os {

// Notify Granary tools that a thread has been created.
void InitThread(void);

// Notify Granary tools that a thread has been destroyed.
void ExitThread(void);

// Yield this thread. This might not actually yield the thread.
void YieldThread(void);

// Get the thread/CPU base address.
//
// Note: This has an architecture-specific implementation.
uintptr_t ThreadBase(void);

}  // namespace os
}  // namespace granary

#endif  // OS_THREAD_H_
