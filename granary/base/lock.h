/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_LOCK_H_
#define GRANARY_BASE_LOCK_H_

#include "granary/base/base.h"

namespace granary {

// Implements a simple atomic spin lock. Spin locks should be used sparingly
// and for fine-grained locking.
class FineGrainedLock {
 public:
  inline FineGrainedLock(void)
      : is_locked(ATOMIC_VAR_INIT(false)) {}

  // Blocks execution by spinning until the lock has been acquired.
  void Acquire(void);

  // Tries to acquire the lock, knowing that the lock is currently contended.
  void ContendedAcquire(void);

  // Returns true if the lock was acquired.
  bool TryAcquire(void);

  // Release the lock. Assumes that the lock is acquired.
  void Release(void);

 private:
  std::atomic<bool> is_locked;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(FineGrainedLock);
};

// Ensures that a lock is held within some scope.
class FineGrainedLocked {
 public:
  inline explicit FineGrainedLocked(FineGrainedLock *lock_)
      : lock(lock_) {
    lock->Acquire();
  }

  inline ~FineGrainedLocked(void) {
    lock->Release();
  }

 private:
  FineGrainedLocked(void) = delete;

  FineGrainedLock *lock;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(FineGrainedLocked);
};

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
class ReadLocked {
 public:
  inline explicit ReadLocked(ReaderWriterLock *lock_)
      : lock(lock_) {
    lock->ReadAcquire();
  }

  inline ~ReadLocked(void) {
    lock->ReadRelease();
  }

 private:
  ReadLocked(void) = delete;

  ReaderWriterLock *lock;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ReadLocked);
};

// Ensures that a write lock is held within some scope.
class WriteLocked {
 public:
  inline explicit WriteLocked(ReaderWriterLock *lock_)
      : lock(lock_) {
    lock->WriteAcquire();
  }

  inline ~WriteLocked(void) {
    lock->WriteRelease();
  }

 private:
  WriteLocked(void) = delete;

  ReaderWriterLock *lock;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(WriteLocked);
};


}  // namespace granary

#endif  // GRANARY_BASE_LOCK_H_