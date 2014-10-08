/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_MEMORY_H_
#define OS_MEMORY_H_

namespace granary {
namespace os {

// Defines the various kinds of available memory protection. This is not an
// exhaustive list, e.g. in practice, one could have all of read, write, and
// execute permissions; however, limiting to these three kinds of protections
// serves as a good discipline.
enum class MemoryProtection {
  PATCHABLE_EXECUTABLE,
  EXECUTABLE = PATCHABLE_EXECUTABLE GRANARY_IF_DEBUG(GRANARY_IF_USER( + 1)),
  READ_ONLY,
  READ_WRITE,
  INACCESSIBLE
};

// The "intent" of allocating these pages. For example, we might intend to
// allocate these pages for executable code, so we will place it somewhere
// special.
enum class MemoryIntent {
  // Memory that is used for typical readable/writable heap objects.
  READ_WRITE,

  // This is used for all allocations that will eventually contain code that
  // can execute.
  EXECUTABLE
};

// Initialize the Granary heap.
void InitHeap(void);

// Destroys the Granary heap.
void ExitHeap(void);

// Allocates `num` number of pages from the OS with `MEMORY_READ_WRITE`
// protection.
void *AllocatePages(int num, MemoryIntent intent=MemoryIntent::READ_WRITE);

// Frees `num` pages back to the OS.
void FreePages(void *, int num, MemoryIntent intent=MemoryIntent::READ_WRITE);

// Changes the memory protection of some pages.
void ProtectPages(void *addr, int num, MemoryProtection prot);

}  // namespace os
}  // namespace granary

#endif  // OS_MEMORY_H_
