/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/control_flow_graph.h"

#include "granary/code/allocate.h"
#include "granary/code/assemble.h"
#include "granary/code/instrument.h"

#include "granary/compile.h"
#include "granary/environment.h"
#include "granary/module.h"
#include "granary/util.h"

namespace granary {

// Compile some code described by its `BlockMetaData` instance.
void Compile(EnvironmentInterface *env, BlockMetaData *meta) {
  LocalControlFlowGraph cfg;
  auto module_meta = MetaDataCast<ModuleMetaData *>(meta);

  // TODO(pag): Would need to put some kind of guard (e.g. r/w lock) around this
  //            function so that on "reads", we write to the code cache, on on
  //            "writes" we flush parts of the cache or kill allocators.
  //            --> This would be well suited towards a lock in the environment.

  Instrument(env, &cfg, meta);
  Assemble(env, &cfg, module_meta->CacheCodeAllocatorForBlock());

  // TODO(pag): Add things into the code cache index.
}

// Initialize the compilation system.
void InitCompiler(void) {}

}  // namespace granary
