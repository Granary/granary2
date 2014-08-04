/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

namespace granary {
namespace arch {

void Relax(void) {
  GRANARY_INLINE_ASSEMBLY("pause;" ::: "memory");
}

void SynchronizePipeline(void) {
  GRANARY_INLINE_ASSEMBLY("cpuid;" ::: "eax", "ebx", "ecx", "edx", "memory");
}

unsigned long CycleCount(void) {
  uint64_t low_order(0);
  uint64_t high_order(0);
  GRANARY_INLINE_ASSEMBLY(
      "rdtscp" : "=a" (low_order), "=d" (high_order) :: "ecx");
  return (high_order << 32U) | low_order;
}

}  // namespace arch
}  // namespace granary
