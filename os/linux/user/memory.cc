/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/lock.h"

#include "granary/breakpoint.h"

#include "os/memory.h"

#define O_CREAT       0100
#define O_RDONLY      00
#define O_RDWR        02
#define O_CLOEXEC     02000000

#define PROT_READ     0x1
#define PROT_WRITE    0x2
#define PROT_EXEC     0x4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_32BIT     0x40

extern "C" {

// Path to the loaded Granary library. Code cache `mmap`s are associated with
// this file.
char granary_mmap_path[1024] = {'\0'};

extern int open(const char *__file, int __oflag, void *);
extern void *mmap(void *__addr, size_t __len, int __prot, int __flags,
                  int __fd, long __offset);
extern int munmap(void *__addr, size_t __len);
extern int mprotect(void *__addr, size_t __len, int __prot);
extern int mlock(const void *__addr, size_t __len);

}  // extern C
namespace granary {
namespace os {
namespace {

static int code_cache_fd = -1;
static SpinLock code_cache_fd_lock;

// Initialize a temporary file descriptor for this code cache. This is so that
// in user space, we can associate the code cache with something in
// `/proc/self/maps`. This isn't needed for the correct function of Granary but
// instead helps to enable recursive instrumentation (Granary instrumenting
// Granary).
//
// TODO(pag): Make this use libgranary.so or granary.out.
static void InitCodeCacheFD(void) {
  SpinLockedRegion locker(&code_cache_fd_lock);
  if (-1 == code_cache_fd) {
    code_cache_fd = open(granary_mmap_path, O_RDONLY, nullptr);
    GRANARY_ASSERT(-1 != code_cache_fd);
  }
}

// For caching MMAPings. Most `operator new`-based allocators are served by
// `os::AllocatePages`, and use this function in the same way. Here, we try
// to coalesce some of those `mmap`s into larger chunks to avoid performing a
// lot of small `mmap`s to grow Granary's heap.
enum {
  MMAP_CACHE_MULT = 64
};
static SpinLock mmap_cache_lock;
static uint8_t *remaining_mem = nullptr;
static int last_prot = 0;
static int last_fd = 0;
static int num_failed_requests = 0;
static int num_remaining_requests = 0;
static size_t remaining_size = 0;

// Initialize the `mmap` cache.
static void *InitMmapCache(size_t num_bytes, int prot, int flags, int fd) {
  auto cache_num_bytes = num_bytes * MMAP_CACHE_MULT;
  uint8_t *ret = reinterpret_cast<uint8_t *>(
      mmap(nullptr, cache_num_bytes, prot, flags, fd, 0));
  last_prot = prot;
  last_fd = fd;
  remaining_size = cache_num_bytes - num_bytes;
  remaining_mem = ret + num_bytes;
  num_failed_requests = 0;
  num_remaining_requests = MMAP_CACHE_MULT - 1;
  return ret;
}

// Try to use the `mmap` cache for an allocation.
static void *TryUseMmapCache(size_t num_bytes, int prot, int flags, int fd) {
  SpinLockedRegion locker(&mmap_cache_lock);
  if (!remaining_size) {
  initialize:
    return InitMmapCache(num_bytes, prot, flags, fd);

  } else if (num_bytes > remaining_size ||
             prot != last_prot ||
             fd != last_fd) {
    num_failed_requests += 1;
    if (num_failed_requests >= (num_remaining_requests / 2)) {
      goto initialize;
    }
    return mmap(nullptr, num_bytes, prot, flags, fd, 0);

  } else if (remaining_size >= num_bytes) {
    auto ret = remaining_mem;
    remaining_size -= num_bytes;
    remaining_mem += num_bytes;
    num_remaining_requests -= 1;
    return ret;

  } else {
    GRANARY_ASSERT(false);
    return nullptr;
  }
}

// A very naive caching version of mmap.
static void *CachingMmap(size_t num_bytes, int prot, int flags, int fd) {
  if (0 != (PROT_EXEC & prot)) {
    return mmap(nullptr, num_bytes, prot, flags, fd, 0);
  }
  return TryUseMmapCache(num_bytes, prot, flags, fd);
}


}  // namespace

// Initialize the Granary heap.
void InitHeap(void) {}

// Destroys the Granary heap.
void ExitHeap(void) {
  // TODO(pag): Unmap all `mmap`ed pages.
}

// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
void *AllocatePages(int num, MemoryIntent intent) {
  auto prot = PROT_READ | PROT_WRITE;
  auto flags = MAP_PRIVATE | MAP_32BIT;
  auto fd = -1;

  if (MemoryIntent::EXECUTABLE == intent) {
    if (GRANARY_UNLIKELY(-1 == code_cache_fd)) {
      InitCodeCacheFD();
    }
    fd = code_cache_fd;
    prot |= PROT_EXEC;

  } else {
    flags |= MAP_ANONYMOUS;
  }

  auto num_bytes = static_cast<size_t>(arch::PAGE_SIZE_BYTES * num);
  auto ret = CachingMmap(num_bytes, prot, flags, fd);
  if (MemoryIntent::EXECUTABLE == intent) {
    mlock(ret, num_bytes);
  }
  return ret;
}

// Frees `num` pages back to the OS.
void FreePages(void *addr, int num, MemoryIntent) {
  munmap(addr, static_cast<size_t>(arch::PAGE_SIZE_BYTES * num));
}

// Changes the memory protection of some pages.
void ProtectPages(void *addr, int num, MemoryProtection prot) {
  int prot_bits(0);
  if (MemoryProtection::PATCHABLE_EXECUTABLE == prot) {
    prot_bits = PROT_EXEC | PROT_READ | PROT_WRITE;
  } else if (MemoryProtection::EXECUTABLE == prot) {
    prot_bits = PROT_EXEC | PROT_READ;
  } else if (MemoryProtection::READ_ONLY == prot) {
    prot_bits = PROT_READ;
  } else if (MemoryProtection::READ_WRITE == prot) {
    prot_bits = PROT_READ | PROT_WRITE;
  } else {
    prot_bits = 0; //  MEMORY_INACCESSIBLE
  }
  mprotect(
      addr,
      static_cast<size_t>(arch::PAGE_SIZE_BYTES * num),
      prot_bits);
}

}  // namespace os
}  // namespace granary
