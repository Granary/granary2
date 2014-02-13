/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/control_flow_graph.h"

#include "granary/code/allocate.h"
#include "granary/code/assemble.h"
#include "granary/code/instrument.h"

#include "granary/compile.h"
#include "granary/metadata.h"

namespace granary {
namespace {
static CodeAllocator instrumented_code(8);
static CodeAllocator edge_code(1);
}  // namespace

// Compile some code described by its `GenericMetaData` instance.
void Compile(GenericMetaData *meta) {
  LocalControlFlowGraph cfg;
  Instrument(&cfg, meta);
  Assemble(&cfg, &instrumented_code, &edge_code);
  // TODO(pag): Add things into the code cache.
}

// Initialize the compilation system.
void InitCompiler(void) {
  //Compile(new GenericMetaData(UnsafeCast<>));
}

}  // namespace granary
