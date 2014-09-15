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

#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20

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
static FineGrainedLock code_cache_fd_lock;

// Initialize a temporary file descriptor for this code cache. This is so that
// in user space, we can associate the code cache with something in
// `/proc/self/maps`. This isn't needed for the correct function of Granary but
// instead helps to enable recursive instrumentation (Granary instrumenting
// Granary).
//
// TODO(pag): Make this use libgranary.so or granary.out.
static void InitCodeCacheFD(void) {
  FineGrainedLocked locker(&code_cache_fd_lock);
  if (-1 == code_cache_fd) {
    code_cache_fd = open(granary_mmap_path, O_RDONLY, nullptr);
    GRANARY_ASSERT(-1 != code_cache_fd);
  }
}

}  // namespace

// Initialize the Granary heap.
void InitHeap(void) {}

// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
void *AllocatePages(int num, MemoryIntent intent) {
  auto prot = PROT_READ | PROT_WRITE;
  auto flags = MAP_PRIVATE;
  auto fd = -1;

  if (MemoryIntent::EXECUTABLE == intent) {
    prot |= PROT_EXEC;
  }

  if (MemoryIntent::EXECUTABLE == intent) {
    if (GRANARY_UNLIKELY(-1 == code_cache_fd)) {
      InitCodeCacheFD();
    }
    fd = code_cache_fd;

  } else {
    flags |= MAP_ANONYMOUS;
  }

  auto num_bytes = static_cast<size_t>(arch::PAGE_SIZE_BYTES * num);
  auto ret = mmap(nullptr, num_bytes, prot, flags, fd, 0);
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
