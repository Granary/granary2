/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_H_
#define GRANARY_CODE_ASSEMBLE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Forward declarations.
class CodeCacheInterface;
class ContextInterface;
class LocalControlFlowGraph;

// Assemble the local control-flow graph.
void Assemble(ContextInterface* env, CodeCacheInterface *code_cache,
              LocalControlFlowGraph *cfg);
}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_H_
