/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/code/edge.h"

namespace granary {

// Enter into Granary to begin the translation process for a direct edge.
void EnterGranary(DirectEdge *edge) {
  if (edge->cached_target) {
    return;  // Already computed the target.
  }
}

}  // namespace granary

extern "C" {

// Convenient C name for `void EnterGranary(DirectEdge *)` above. This name
// is referenced by assembly files.
void granary_enter_direct_edge(void)
    __attribute__((alias ("_ZN7granary12EnterGranaryEPNS_10DirectEdgeE")));

}  // extern C
