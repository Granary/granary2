/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_1_MANGLE_H_
#define GRANARY_CODE_ASSEMBLE_1_MANGLE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Forward declarations.
class LocalControlFlowGraph;

// Relativize the native instructions within a LCFG.
void MangleInstructions(LocalControlFlowGraph* cfg);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_1_MANGLE_H_
