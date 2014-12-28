/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_LOCK_H_
#define OS_LOCK_H_

#ifdef GRANARY_OS_linux
# ifdef GRANARY_WHERE_user
#   include "os/linux/user/lock.h"
# else
#   include "os/linux/kernel/lock.h"
# endif
#else
# error "Unsupported operating system."
#endif

#include "granary/base/base.h"
#include "granary/breakpoint.h"

namespace granary {
namespace os {

// Ensures that a lock is held within some scope.
class LockedRegion {
 public:
  inline explicit LockedRegion(Lock *lock_)
      : lock(lock_) {
    lock->Acquire();
  }

  inline ~LockedRegion(void) {
    lock->Release();
  }

 private:
  LockedRegion(void) = delete;

  Lock * const  lock;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(LockedRegion);
};

}  // namespace os
}  // namespace granary

#endif  // OS_LOCK_H_
