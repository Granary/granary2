/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ASSEMBLE_0_COMPILE_INLINE_ASSEMBLY_H_
#define GRANARY_CODE_ASSEMBLE_0_COMPILE_INLINE_ASSEMBLY_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Forward declaration.
class Trace;

// Compile all inline assembly instructions by parsing the inline assembly
// instructions and doing code generation for them.
void CompileInlineAssembly(Trace *cfg);

}  // namespace granary

#endif  // GRANARY_CODE_ASSEMBLE_0_COMPILE_INLINE_ASSEMBLY_H_
