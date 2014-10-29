/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_USER_MALLOC_FREE_H_
#define CLIENTS_USER_MALLOC_FREE_H_

// Context to a malloc function call.
class MallocContext {
 public:
  inline MallocContext(granary::arch::MachineContext *context_)
      : context(context_) {}

  inline uint64_t &NumBytes(void) const {
    return *granary::os::abi::NthSystemCallArgument(context, 0);
  }
  inline uint64_t &AllocatedAddress(void) const {
    return *granary::os::abi::SystemCallReturnValue(context);
  }
 private:
  granary::arch::MachineContext * const context;
};

#endif  // CLIENTS_USER_MALLOC_FREE_H_
