/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/control_flow_graph.h"

#include "granary/code/allocate.h"
#include "granary/code/assemble.h"
#include "granary/code/instrument.h"

#include "granary/compile.h"
#include "granary/environment.h"
#include "granary/util.h"

namespace granary {

// Compile some code described by its `GenericMetaData` instance.
void Compile(Environment *env, GenericMetaData *meta) {
  LocalControlFlowGraph cfg;
  auto translation_meta = MetaDataCast<TranslationMetaData *>(meta);
  auto module = translation_meta->source.module;

  Instrument(env, &cfg, meta);
  Assemble(
      &cfg,
      module->cache_code_allocator,
      env->edge_code_allocator);

  // TODO(pag): Add things into the code cache index.
}

// Initialize the compilation system.
void InitCompiler(void) {}

}  // namespace granary
