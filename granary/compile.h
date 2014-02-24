/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_COMPILE_H_
#define GRANARY_COMPILE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Forward declarations.
class EnvironmentInterface;
class BlockMetaData;

// Compile some code described by its `BlockMetaData` instance within the
// environment `env`.
void Compile(EnvironmentInterface *env, BlockMetaData *meta);

// Initialize the compilation system.
void InitCompiler(void);

}  // namespace granary

#endif  // GRANARY_COMPILE_H_
