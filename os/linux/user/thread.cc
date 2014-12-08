/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "os/thread.h"

#include "granary/init.h"
#include "granary/tool.h"

namespace granary {
namespace os {

// Notify Granary tools that a thread has been created.
//
// When new threads are created via the `clone` system call, this function
// will *return* to instrumented code. The way this works is that the `user`
// client instruments the function pointer associated with the `clone`
// before the `clone` is made.
void InitThread(void) {
  InitTools(kInitThread);
}

// Notify Granary tools that a thread has been destroyed.
void ExitThread(void) {
  ExitTools(kExitThread);
}

}  // namespace os
}  // namespace granary

