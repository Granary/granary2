/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"

namespace granary {
namespace cpu {

void Relax(void) {
  GRANARY_INLINE_ASSEMBLY("pause;" ::: "memory");
}

void SynchronizePipeline(void) {
  GRANARY_INLINE_ASSEMBLY("cpuid;" ::: "eax", "ebx", "ecx", "edx", "memory");
}

}  // namespace cpu
}  // namespace granary
