/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <sys/mman.h>

#include "granary/arch/base.h"
#include "granary/base/base.h"
#include "granary/memory.h"

#ifndef PROT_NONE
# define PROT_NONE 0
#endif
#ifndef MAP_ANONYMOUS
# ifdef MAP_ANON
#   define MAP_ANONYMOUS MAP_ANON
# else
#   define MAP_ANONYMOUS 0
# endif
#endif
#ifndef MAP_SHARED
# define MAP_SHARED 0
#endif

namespace granary {

// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
void *AllocatePages(int num, MemoryIntent intent) {
  auto prot = MemoryIntent::EXECUTABLE == intent ? PROT_WRITE :
                                                   PROT_READ | PROT_WRITE;
  void *ret(mmap(
      nullptr,
      static_cast<size_t>(arch::PAGE_SIZE_BYTES * num),
      prot,
      MAP_PRIVATE | MAP_ANONYMOUS,
      -1,
      0));

  return ret;
}

// Frees `num` pages back to the OS.
void FreePages(void *addr, int num, MemoryIntent) {
  munmap(addr, static_cast<size_t>(arch::PAGE_SIZE_BYTES * num));
}

// Changes the memory protection of some pages.
void ProtectPages(void *addr, int num, MemoryProtection prot) {
  int prot_bits(0);
  if (MemoryProtection::EXECUTABLE == prot) {
    prot_bits = PROT_EXEC;
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

}  // namespace granary
