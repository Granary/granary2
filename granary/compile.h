/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_COMPILE_H_
#define GRANARY_COMPILE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Forward declarations.
class GenericMetaData;

// Compile some code described by its `GenericMetaData` instance.
void Compile(GenericMetaData *);

// Initialize the compilation system.
void InitCompiler(void);

}  // namespace granary

#endif  // GRANARY_COMPILE_H_
