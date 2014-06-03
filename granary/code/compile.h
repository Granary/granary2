/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_COMPILE_H_
#define GRANARY_CODE_COMPILE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/code/fragment.h"

namespace granary {

// Forward declaration.
class CodeCacheInterface;

// Compile some instrumented code.
void Compile(LocalControlFlowGraph *cfg, CodeCacheInterface *edge_cache);

}  // namespace granary

#endif  // GRANARY_CODE_COMPILE_H_
