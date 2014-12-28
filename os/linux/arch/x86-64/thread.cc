/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "os/thread.h"

extern "C" {
extern uintptr_t granary_arch_get_segment_base(void);
}  // extern C
namespace granary {
namespace os {

// Get the thread/CPU base address.
uintptr_t ThreadBase(void) {
  return granary_arch_get_segment_base();
}

}  // namespace os
}  // namespace granary
