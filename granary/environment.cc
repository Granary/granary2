/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/code/metadata.h"

#include "granary/environment.h"

GRANARY_DEFINE_string(tools, "",
    "Comma-seprated list of tools to dynamically load on start-up. "
    "For example: `--clients=print_bbs,follow_jumps`.");

GRANARY_DEFINE_positive_int(edge_cache_slab_size, 1,
    "The number of pages allocated at once to store edge code. Each "
    "environment maintains its own edge code allocator. The default value is "
    "1 pages per slab.");

namespace granary {

enum {
  MAX_TOOL_NAME_LEN = 32
};

// Initialize a new environment.
Environment::Environment(void)
    : module_manager(&context),
      metadata_manager(),
      tool_manager(),
      edge_code_cache(FLAG_edge_cache_slab_size),
      context(&module_manager, &metadata_manager,
              &tool_manager, &edge_code_cache) {}

// Setup this environment for Granary-based instrumentation.
void Environment::Setup(void) {
  // Register internal metadata.
  metadata_manager.Register<ModuleMetaData>();
  metadata_manager.Register<CacheMetaData>();
  metadata_manager.Register<StackMetaData>();

  // Tell this environment about all loaded modules.
  module_manager.RegisterAllBuiltIn();

  // Tell Granary about all loaded tools.
  ForEachCommaSeparatedString<MAX_TOOL_NAME_LEN>(
      FLAG_tools,
      [&] (const char *tool_name) {
        tool_manager.Register(tool_name);
      });

  // Do a dummy allocation and free of all tools. Tools register meta-data
  // through their constructors and so this will get all tool+option-specific
  // meta-data registered.
  tool_manager.FreeTools(tool_manager.AllocateTools(&context));
}

void Environment::Attach(void) {

}

void Environment::AttachToAppPC(AppPC pc) {
  context.Compile(context.AllocateBlockMetaData(pc));
}

}  // namespace granary
