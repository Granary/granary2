/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_LOCK_H_
#define GRANARY_BASE_LOCK_H_

#include "granary/base/base.h"

namespace granary {

// Implements a simple ticket spin lock. Spin locks should be used sparingly
// and only for fine-grained locking.
class SpinLock {
 public:
  inline SpinLock(void)
      : serving_ticket(ATOMIC_VAR_INIT(0)),
        next_ticket(ATOMIC_VAR_INIT(0)) {}

  // Blocks execution by spinning until the lock has been acquired.
  void Acquire(void);

  // Release the lock. Assumes that the lock is acquired.
  void Release(void);

 private:

  std::atomic<int> serving_ticket;
  std::atomic<int> next_ticket;

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

  bool TryReadAcquire(void);
  void ReadAcquire(void);
  void ReadRelease(void);

  bool TryWriteAcquire(void);
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
