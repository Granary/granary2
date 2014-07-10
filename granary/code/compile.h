/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_COMPILE_H_
#define GRANARY_CODE_COMPILE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Forward declaration.
class ContextInterface;
class IndirectEdge;
class LocalControlFlowGraph;

// Compile some instrumented code.
void Compile(ContextInterface *context, LocalControlFlowGraph *cfg);

// Compile some instrumented code for an indirect edge.
void Compile(ContextInterface *context, LocalControlFlowGraph *cfg,
             IndirectEdge *edge, AppPC target_pc);

}  // namespace granary

#endif  // GRANARY_CODE_COMPILE_H_
