/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_CODE_ALLOCATE_H_
#define GRANARY_CODE_ALLOCATE_H_

#ifndef GRANARY_INTERNAL
# error "Can only be used in internal Granary code."
#endif

#include "granary/base/types.h"

namespace granary {

// A request to allocate some code. This approximates where the final code
// will be placed.
class CodeAllocationRequest {
 private:
  CacheProgramCounter begin;  // Estimated
  int size;  // Overestimates how much memory is needed.
};

// Used to allocate code.
class CodeAllocator {
 private:

};

}  // namespace granary

#endif  // GRANARY_CODE_ALLOCATE_H_
