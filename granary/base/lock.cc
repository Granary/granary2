/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/cpu.h"

#include "granary/base/lock.h"

#include "os/thread.h"

namespace granary {

// Acquires the lock, knowing that the lock is currently contended.
void SpinLock::ContendedAcquire(void) {
  for (; is_locked.load(std::memory_order_relaxed) || !TryAcquire(); );
}

// Returns true if the lock was acquired.
bool SpinLock::TryAcquire(void) {
  if (is_locked.exchange(true, std::memory_order_acquire)) {
    arch::Relax();
    return false;
  }
  return true;
}

// Release the lock. Assumes that the lock is acquired.
void SpinLock::Release(void) {
  is_locked.store(false, std::memory_order_release);
}

// Read-side acquire.
bool ReaderWriterLock::TryReadAcquire(void) {
  uint32_t old_value = 0U;
  do {
    old_value = lock.load(std::memory_order_relaxed) & 0x7fffffffU;
    if (lock.compare_exchange_weak(old_value, old_value + 1,
                                   std::memory_order_release,
                                   std::memory_order_relaxed)) {
      return true;
    }
  } while (0x80000000U > old_value);  // Reader-reader contention.
  return false;
}

// Read-side acquire.
void ReaderWriterLock::ReadAcquire(void) {
  for (; !TryReadAcquire(); ) {
    os::YieldThread();
  }
}

// Read-side release.
void ReaderWriterLock::ReadRelease(void) {
  lock.fetch_sub(1U, std::memory_order_release);
}

// Write-side acquire.
bool ReaderWriterLock::TryWriteAcquire(void) {
  auto old_value = 0U;  // No contending writers, no contending readers.
  return lock.compare_exchange_weak(old_value, 0x80000000U,
                                    std::memory_order_release,
                                    std::memory_order_relaxed);
}

// Write-side acquire.
void ReaderWriterLock::WriteAcquire(void) {
  uint32_t old_value(0);

  for (;;) {
    old_value = lock.load(std::memory_order_acquire) & 0x7fffffffU;
    if (lock.compare_exchange_weak(old_value, old_value | 0x80000000,
                                   std::memory_order_release,
                                   std::memory_order_relaxed)) {
      break;
    }
    os::YieldThread();
  }

  while (lock.load(std::memory_order_acquire) & 0x7fffffff) {
    os::YieldThread();
  }
}

// Write-side release.
void ReaderWriterLock::WriteRelease(void) {
  lock.store(0, std::memory_order_release);
}

}  // namespace granary
