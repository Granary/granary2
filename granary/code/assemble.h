/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_H_
#define GRANARY_CODE_ASSEMBLE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/code/fragment.h"

namespace granary {

// Forward declarations.
class CodeCache;
class ContextInterface;
class LocalControlFlowGraph;

// Assemble the local control-flow graph.
FragmentList Assemble(ContextInterface *context,
                      CodeCache *code_cache,
                      LocalControlFlowGraph *cfg);
}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_H_
