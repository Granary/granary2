/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_COMPILE_H_
#define GRANARY_CODE_COMPILE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Forward declaration.
class Context;
class IndirectEdge;
class LocalControlFlowGraph;

// Compile some instrumented code.
CachePC Compile(Context *context, LocalControlFlowGraph *cfg);

// Compile some instrumented code for an indirect edge.
CachePC Compile(Context *context, LocalControlFlowGraph *cfg,
                IndirectEdge *edge, BlockMetaData *meta);

}  // namespace granary

#endif  // GRANARY_CODE_COMPILE_H_
