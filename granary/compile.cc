/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/cast.h"

#include "granary/cfg/control_flow_graph.h"

#include "granary/code/allocate.h"
#include "granary/code/assemble.h"
#include "granary/code/instrument.h"

#include "granary/compile.h"
#include "granary/metadata.h"

namespace granary {
namespace {

// Represents the "code cache", i.e. all instrumented code is managed by this
// allocator.
GRANARY_EARLY_GLOBAL static CodeAllocator instrumented_code(8);

// Represents "edge code" that is used to resolve direct/indirect branching.
GRANARY_EARLY_GLOBAL static CodeAllocator edge_code(1);
}  // namespace

// Compile some code described by its `GenericMetaData` instance.
void Compile(GenericMetaData *meta) {
  LocalControlFlowGraph cfg;
  Instrument(&cfg, meta);
  Assemble(&cfg, &instrumented_code, &edge_code);
  // TODO(pag): Add things into the code cache index.
}

// Initialize the compilation system.
void InitCompiler(void) {
  Compile(new GenericMetaData(UnsafeCast<AppPC>(&Compile)));
}

}  // namespace granary
