/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/memory.h"

extern "C" {

// Linux kernel interfaces for changing the page protection of memory.
// From: arch/x86/mm/pageattr.c.
//
// TODO(pag): These are not cross-platform.
// TODO(pag): These APIs do not provide mutual exclusion over modifying page
//            protection. This could lead to issues both where two instances
//            of Granary modify the protections of nearby memory, and where
//            the Granary and the kernel compete to modify some mappings.
extern int set_memory_x(unsigned long addr, int numpages);
extern int set_memory_nx(unsigned long addr, int numpages);

extern int set_memory_ro(unsigned long addr, int numpages);
extern int set_memory_rw(unsigned long addr, int numpages);
}

namespace granary {

// Changes the memory protection of some pages.
void ProtectPages(void *addr_, int num, MemoryProtection prot) {
  auto addr = reinterpret_cast<unsigned long>(addr_);
  if (MemoryProtection::EXECUTABLE == prot) {
    set_memory_x(addr, num);
  } else if (MemoryProtection::READ_ONLY == prot) {
    set_memory_ro(addr, num);
  } else if (MemoryProtection::READ_WRITE == prot) {
    set_memory_rw(addr, num);
  }
}

}  // namespace granary
