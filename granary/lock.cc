/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/arch/cpu.h"
#include "granary/lock.h"

namespace granary {

// Blocks execution by spinning until the lock has been acquired.
void FineGrainedLock::Acquire(void) {
  if (TryAcquire()) {
    return;
  }
  ContendedAcquire();
}

// Tries to acquire the lock, knowing that the lock is currently contended.
void FineGrainedLock::ContendedAcquire(void) {
  do {
    cpu::Relax();
  } while (is_locked.load(std::memory_order_relaxed) || !TryAcquire());
}

// Returns true if the lock was acquired.
bool FineGrainedLock::TryAcquire(void) {
  return !is_locked.exchange(true, std::memory_order_acquire);
}

// Release the lock. Assumes that the lock is acquired.
void FineGrainedLock::Release(void) {
  is_locked.store(false, std::memory_order_release);
}

// Read-side acquire.
void ReaderWriterLock::ReadAcquire(void) {
  for (;;) {
    if (!writer_count.load(std::memory_order_acquire)) {
      reader_count.fetch_add(1, std::memory_order_acquire);
      break;
    }
    cpu::Relax();
  }
}

// Read-side release.
void ReaderWriterLock::ReadRelease(void) {
  reader_count.fetch_sub(1, std::memory_order_release);
}

// Write-side acquire.
void ReaderWriterLock::WriteAcquire(void) {
  if (!writer_count.fetch_add(1, std::memory_order_acquire)) {
    writer_lock.Acquire();
  } else {
    writer_lock.ContendedAcquire();
  }

  // We're holding the write lock, now block on any concurrent readers.
  while (reader_count.load(std::memory_order_acquire)) {
    cpu::Relax();
  }
}

// Write-side release.
void ReaderWriterLock::WriteRelease(void) {
  writer_lock.Release();
  writer_count.fetch_sub(1, std::memory_order_release);
}

}  // namespace granary
