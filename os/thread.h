/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_THREAD_H_
#define OS_THREAD_H_

namespace granary {
namespace os {

// Notify Granary tools that a thread has been created.
void InitThread(void);

// Notify Granary tools that a thread has been destroyed.
void ExitThread(void);

}  // namespace os
}  // namespace granary

#endif  // OS_THREAD_H_
