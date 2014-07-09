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

// The kind of instrumentation request. That is, are we trying to instrument
// a block that is untargeted / targeted by a direct control flow instruction,
// or a block that is targeted by an indirect control-flow instruction?
//
// The major difference between direct/indirect is that with indirect, we need
// to give clients/tools the option to jump to native code, which requires a
// compensation basic block. Another issue is that of specialized returns, or
// things like `longjmp`, where we want the target of the indirect jump might
// be an address in the code cache. In these cases, we want to combine the
// meta-datas together.
enum InstrumentRequestKind {
  INSTRUMENT_DIRECT,
  INSTRUMENT_INDIRECT
};

// Instrument some initial code (described by `meta`) and fills the LCFG `cfg`
// with the instrumented code. `meta` is taken as being "owned", i.e. no one
// should be concurrently modifying `meta`!
//
// Note: `meta` might be deleted if some block with the same meta-data already
//       exists in the code cache index. Therefore, one must use the returned
//       meta-data hereafter.
BlockMetaData *Instrument(ContextInterface *env, LocalControlFlowGraph *cfg,
                          BlockMetaData *meta,
                          InstrumentRequestKind kind=INSTRUMENT_DIRECT);

}  // namespace granary

#endif  // GRANARY_INSTRUMENT_H_
