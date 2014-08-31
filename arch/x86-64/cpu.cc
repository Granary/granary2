/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"
#include "granary/breakpoint.h"

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

bool TryDisableInterrupts(void) {
  unsigned long flags;
  GRANARY_INLINE_ASSEMBLY("pushfq; pop %0; cli" : "=r"(flags));
  return 0UL != (flags & (1UL << 9));
}

void EnableInterrupts(void) {
  GRANARY_INLINE_ASSEMBLY("sti");
}

bool TryDisablePageProtection(void) {
  unsigned long value;
  GRANARY_INLINE_ASSEMBLY("mov %%cr0, %0" : "=r" (value));
  if (0UL == (value & 0x00010000UL)) return false;
  GRANARY_INLINE_ASSEMBLY("mov %0, %%cr0" : : "r" (value & ~0x00010000));
  return true;
}

void EnablePageProtection(void) {
  unsigned long value;
  GRANARY_INLINE_ASSEMBLY("mov %%cr0, %0" : "=r" (value));
  if (0UL != (value & 0x00010000UL)) return;
  GRANARY_INLINE_ASSEMBLY("mov %0, %%cr0" : : "r" (value | 0x00010000));
}

}  // namespace arch
}  // namespace granary
