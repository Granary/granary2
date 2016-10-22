/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/cpu.h"

#include "os/linux/user/lock.h"

extern "C" {
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
struct timespec;
int sys_futex(uint32_t *uaddr, int op, uint32_t val,
              const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3);

}  // extern C
namespace granary {
namespace os {

// This code adapted from:
// http://bartoszmilewski.com/2008/09/01/thin-lock-vs-futex/

#define CAS(a,b,c) __sync_bool_compare_and_swap(a, b, c)

void Lock::Acquire(void) {
  auto state_ptr = reinterpret_cast<uint32_t *>(&state);
  if (CAS(state_ptr, kLockUncontended, kLockAcquired)) {
    return;  // Acquired.
  }
  do {
    arch::SynchronizePipeline();

    // Assume lock is still taken, try to make it 2 and wait
    if (kLockContended == state ||
        CAS(state_ptr, kLockAcquired, kLockContended)) {

      // Wait, but only if the lock is still contended.
      sys_futex(state_ptr, FUTEX_WAIT, kLockContended, nullptr, nullptr, 0);
    }
    // Try (again) assuming the lock is free.
  } while (!CAS(state_ptr, kLockUncontended, kLockContended));
}

void Lock::Release(void) {
  auto state_ptr = reinterpret_cast<uint32_t *>(&state);
  if (kLockContended == __sync_fetch_and_sub(state_ptr, 1U)) {
    state = kLockUncontended;  // Release the lock.

    // Unfairly wake up an arbitrary thread.
    sys_futex(state_ptr, FUTEX_WAKE, 1, nullptr, nullptr, 0);
  }
}

}  // namespace os
}  // namespace granary
