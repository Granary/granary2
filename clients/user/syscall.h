/* Copyright 2012-2014 Peter Goodman, all rights reserved. */
/*
 * syscall.h
 *
 *  Created on: Sep 8, 2014
 *      Author: Peter Goodman
 */
#ifndef CLIENTS_USER_SYSCALL_H_
#define CLIENTS_USER_SYSCALL_H_

#include <granary.h>

// Represents a system call context that gives relatively easy access to
// system call numbers, arguments, and return values.
class SystemCallContext {
 public:
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
  inline int Number(void) const {
    return static_cast<int>(*granary::os::abi::SystemCallNumber(context));
  }
 private:
  granary::arch::MachineContext * const context;
};

// Callback types for system calls. The exit hook is interesting because it
// gives access the machine context prior to the system call (so that arguments,
// if clobbered by the syscall), can be correctly found.
typedef void (SysCallEntryHook)(SystemCallContext context);
typedef void (SysCallExitHook)(SystemCallContext pre_context,
                               SystemCallContext post_context);

// Register a function to be called before a system call is made.
void OnSystemCallEntry(SysCallEntryHook *hook);

// Register a function to be called after a system call is made.
void OnSystemCallExit(SysCallExitHook *hook);

#endif /* CLIENTS_USER_SYSCALL_H_ */
