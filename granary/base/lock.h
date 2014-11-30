/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_LOCK_H_
#define GRANARY_BASE_LOCK_H_

#include "granary/base/base.h"

namespace granary {

// Implements a simple atomic spin lock. Spin locks should be used sparingly
// and for fine-grained locking.
class SpinLock {
 public:
  inline SpinLock(void)
      : is_locked(ATOMIC_VAR_INIT(false)) {}

  // Blocks execution by spinning until the lock has been acquired.
  bool TryAcquire(void);

  // Blocks execution by spinning until the lock has been acquired.
  inline void Acquire(void) {
    if (TryAcquire()) return;
    ContendedAcquire();
  }

  // Release the lock. Assumes that the lock is acquired.
  void Release(void);

 private:

  // Acquires the lock, knowing that the lock is currently contended.
  void ContendedAcquire(void);

  std::atomic<bool> is_locked;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(SpinLock);
};

// Ensures that a lock is held within some scope.
class SpinLockedRegion {
 public:
  inline explicit SpinLockedRegion(SpinLock *lock_)
      : lock(lock_) {
    lock->Acquire();
  }

  inline ~SpinLockedRegion(void) {
    lock->Release();
  }

 private:
  SpinLockedRegion(void) = delete;

  SpinLock * const  lock;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(SpinLockedRegion);
};

#define GRANARY_LOCKED(lock_name, ...) \
  do { \
    FineGrainedLocked locker(&(lock_name)); \
    __VA_ARGS__ \
  } while (0)

// Implements a fine-grained reader/writer lock.
class ReaderWriterLock {
 public:
  inline ReaderWriterLock(void)
      : lock(ATOMIC_VAR_INIT(0)) {}

  void ReadAcquire(void);
  void ReadRelease(void);

  void WriteAcquire(void);
  void WriteRelease(void);

 private:

  std::atomic<uint32_t> lock;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ReaderWriterLock);
};

// Ensures that a read lock is held within some scope.
class ReadLockedRegion {
 public:
  inline explicit ReadLockedRegion(ReaderWriterLock *lock_)
      : lock(lock_) {
    lock->ReadAcquire();
  }

  inline ~ReadLockedRegion(void) {
    lock->ReadRelease();
  }

 private:
  ReadLockedRegion(void) = delete;

  ReaderWriterLock * const lock;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ReadLockedRegion);
};

// Ensures that a write lock is held within some scope.
class WriteLockedRegion {
 public:
  inline explicit WriteLockedRegion(ReaderWriterLock *lock_)
      : lock(lock_) {
    lock->WriteAcquire();
  }

  inline ~WriteLockedRegion(void) {
    lock->WriteRelease();
  }

 private:
  WriteLockedRegion(void) = delete;

  ReaderWriterLock * const lock;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(WriteLockedRegion);
};

}  // namespace granary

#endif  // GRANARY_BASE_LOCK_H_
