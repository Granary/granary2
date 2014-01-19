/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_BASE_MEMORY_H_
#define GRANARY_BASE_MEMORY_H_

namespace granary {

// Defines the various kinds of available memory protection. This is not an
// exhaustive list, e.g. in practice, one could have all of read, write, and
// execute permissions; however, limiting to these three kinds of protections
// serves as a good discipline.
enum class MemoryProtection {
  MEMORY_EXECUTABLE,  // Implies read-only status.
  MEMORY_READ_ONLY,
  MEMORY_READ_WRITE,
  MEMORY_INACCESSIBLE
};


// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
void *AllocatePages(int num);


// Frees `num` pages back to the OS.
void FreePages(void *, int num);


// Changes the memory protection of some pages.
void ProtectPages(void *addr, int num, MemoryProtection prot);

}  // namespace granary

#endif  // GRANARY_BASE_MEMORY_H_
