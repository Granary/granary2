/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_COMPILE_H_
#define GRANARY_COMPILE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Forward declarations.
class Environment;
class GenericMetaData;

// Compile some code described by its `GenericMetaData` instance within the
// environment `env`.
void Compile(Environment *env, GenericMetaData *meta);

// Initialize the compilation system.
void InitCompiler(void);

}  // namespace granary

#endif  // GRANARY_COMPILE_H_
