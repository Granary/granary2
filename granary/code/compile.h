/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_COMPILE_H_
#define GRANARY_CODE_COMPILE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Forward declaration.
class ContextInterface;
class LocalControlFlowGraph;

// Compile some instrumented code.
void Compile(ContextInterface *context, LocalControlFlowGraph *cfg);

}  // namespace granary

#endif  // GRANARY_CODE_COMPILE_H_
