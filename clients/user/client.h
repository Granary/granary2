/* Copyright 2012-2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_USER_CLIENT_H_
#define CLIENTS_USER_CLIENT_H_

#include <granary.h>

#ifdef GRANARY_WHERE_user

// Represents a system call context that gives relatively easy access to
// system call numbers, arguments, and return values.
class SystemCallContext {
 public:
  inline explicit SystemCallContext(granary::arch::MachineContext *context_)
      : context(context_) {}

  inline uint64_t &Arg0(void) const {
    return *granary::os::abi::NthSystemCallArgument(context, 0);
  }
  inline uint64_t &Arg1(void) const {
    return *granary::os::abi::NthSystemCallArgument(context, 1);
  }
  inline uint64_t &Arg2(void) const {
    return *granary::os::abi::NthSystemCallArgument(context, 2);
  }
  inline uint64_t &Arg3(void) const {
    return *granary::os::abi::NthSystemCallArgument(context, 3);
  }
  inline uint64_t &Arg4(void) const {
    return *granary::os::abi::NthSystemCallArgument(context, 4);
  }
  inline uint64_t &Arg5(void) const {
    return *granary::os::abi::NthSystemCallArgument(context, 5);
  }
  inline uint64_t &ReturnValue(void) const {
    return *granary::os::abi::SystemCallReturnValue(context);
  }
  inline uint64_t &Number(void) const {
    return *granary::os::abi::SystemCallNumber(context);
  }
 private:
  SystemCallContext(void) = delete;

  granary::arch::MachineContext * const context;
};

// Callback types for system calls. The exit hook is interesting because it
// gives access the machine context prior to the system call (so that arguments,
// if clobbered by the syscall), can be correctly found.
typedef void (SystemCallHook)(SystemCallContext context);

// Register a function to be called before a system call is made. `data` is
// a pointer to some opaque data structure which will be passed to the callback.
// `delete_data` is a function that will clean up `data`s memory when the
// hook is removed.
void AddSystemCallEntryFunction(SystemCallHook *hook);

// Register a function to be called after a system call is made. `data` is
// a pointer to some opaque data structure which will be passed to the callback.
// `delete_data` is a function that will clean up `data`s memory when the
// hook is removed.
void AddSystemCallExitFunction(SystemCallHook *hook);
#endif  // GRANARY_WHERE_user

#endif  // CLIENTS_USER_CLIENT_H_
