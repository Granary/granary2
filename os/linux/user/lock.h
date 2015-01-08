/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LINUX_USER_LOCK_H_
#define OS_LINUX_USER_LOCK_H_

#include "granary/base/new.h"

namespace granary {
namespace os {

// Represents a "fat" lock. In user space, this is a futex.
class alignas(sizeof(uint32_t)) Lock {
 public:
  inline Lock(void)
      : state(kLockUncontended) {}

  // Blocks execution until the lock has been acquired.
  void Acquire(void);

  // Release the lock. Assumes that the lock is acquired.
  void Release(void);

  GRANARY_DEFINE_NEW_ALLOCATOR(Lock, {
    kAlignment = 1
  })

 private:
  // States of the lock.
  enum alignas(sizeof(uint32_t)) LockState : uint32_t {
    kLockUncontended = 0,
    kLockAcquired = 1,
    kLockContended = 2
  };

  LockState state;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Lock);
};

}  // namespace os
}  // namespace granary

#endif  // OS_LINUX_USER_LOCK_H_
