/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_INSTRUMENT_H_
#define GRANARY_INSTRUMENT_H_

#include "granary/base/base.h"

namespace granary {

class LocalControlFlowGraph;
class GenericMetaData;

// Instrument some initial code (described by `meta`) and fills the LCFG `cfg`
// with the instrumented code. `meta` is taken as a `unique_ptr` to communicate
// that `Instrument` takes ownership of the meta-data.
void Instrument(LocalControlFlowGraph *cfg,
                std::unique_ptr<GenericMetaData> meta);

// Initialize the instrumentation system. This goes and checks if any tools
// are defined that might actually want to instrument code in one way or
// another.
void InitInstrumentation(void);

}  // namespace granary

#endif  // GRANARY_INSTRUMENT_H_
