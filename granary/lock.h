/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_LOCK_H_
#define GRANARY_LOCK_H_

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
};

// Implements a fine-grained reader/writer lock.
class ReaderWriterLock {
 public:
  inline ReaderWriterLock(void)
      : writer_lock(),
        reader_count(ATOMIC_VAR_INIT(0)) {}

  void ReadAcquire(void);
  void ReadRelease(void);

  void WriteAcquire(void);
  void WriteRelease(void);

 private:
  FineGrainedLock writer_lock;

  std::atomic<int> reader_count;
  std::atomic<int> writer_count;
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
};


}  // namespace granary

#endif  // GRANARY_LOCK_H_
