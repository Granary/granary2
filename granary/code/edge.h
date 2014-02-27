/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_EDGE_H_
#define GRANARY_CODE_EDGE_H_

#ifndef GRANARY_INTERNAL
# define "This code is internal to Granary."
#endif

#include "granary/base/pc.h"

namespace granary {

// Forward declarations.
class BlockMetaData;
class ContextInterface;

// Must be implemented in each Granary driver.
CachePC AssembleEdge(ContextInterface *env, BlockMetaData *meta);

}  // namespace granary

#endif  // GRANARY_CODE_EDGE_H_
