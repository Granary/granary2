/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_INSTRUMENT_H_
#define GRANARY_CODE_INSTRUMENT_H_

namespace granary {

// Forward declarations.
class LocalControlFlowGraph;
class BlockMetaData;
class EnvironmentInterface;

// Instrument some initial code (described by `meta`) and fills the LCFG `cfg`
// with the instrumented code. `meta` is taken as being "owned", i.e. no one
// should be concurrently modifying `meta`!
void Instrument(EnvironmentInterface *env, LocalControlFlowGraph *cfg,
                BlockMetaData *meta);

}  // namespace granary

#endif  // GRANARY_CODE_INSTRUMENT_H_
