/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_EDGE_H_
#define GRANARY_CODE_EDGE_H_

#ifndef GRANARY_INTERNAL
# define "This code is internal to Granary."
#endif

#include "granary/base/types.h"

namespace granary {

// Forward declarations.
class CodeAllocator;
class GenericMetaData;

// Must be implemented in each Granary driver.
CacheProgramCounter AssembleEdge(CodeAllocator *allocator,
                                 GenericMetaData *meta);

}  // namespace granary

#endif  // GRANARY_CODE_EDGE_H_
