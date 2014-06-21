/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_INSTRUMENT_H_
#define GRANARY_INSTRUMENT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Forward declarations.
class LocalControlFlowGraph;
class BlockMetaData;
class ContextInterface;

// Instrument some initial code (described by `meta`) and fills the LCFG `cfg`
// with the instrumented code. `meta` is taken as being "owned", i.e. no one
// should be concurrently modifying `meta`!
//
// Note: `meta` might be deleted if some block with the same meta-data already
//       exists in the code cache index. Therefore, one must use the returned
//       meta-data hereafter.
BlockMetaData *Instrument(ContextInterface *env, LocalControlFlowGraph *cfg,
                          BlockMetaData *meta);

}  // namespace granary

#endif  // GRANARY_INSTRUMENT_H_
