/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/cfg/basic_block.h"

#include "granary/code/compile.h"
#include "granary/code/edge.h"
#include "granary/code/metadata.h"

#include "granary/cache.h"
#include "granary/context.h"

GRANARY_DEFINE_string(tools, "",
    "Comma-seprated list of tools to dynamically load on start-up. "
    "For example: `--clients=print_bbs,follow_jumps`.");

GRANARY_DEFINE_positive_int(edge_cache_slab_size, 1,
    "The number of pages allocated at once to store edge code. Each "
    "environment maintains its own edge code allocator. The default value is "
    "1 pages per slab.");

namespace granary {

ContextInterface::ContextInterface(void) {}

Context::Context(void)
    : module_manager(this),
      metadata_manager(),
      tool_manager(this),
      edge_code_cache(FLAG_edge_cache_slab_size) {

  // Register internal metadata.
  metadata_manager.Register<ModuleMetaData>();
  metadata_manager.Register<CacheMetaData>();
  metadata_manager.Register<LiveRegisterMetaData>();
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
  tool_manager.FreeTools(tool_manager.AllocateTools());
}

// Allocate and initialize some `BlockMetaData`. This will also set-up the
// `ModuleMetaData` within the `BlockMetaData`.
BlockMetaData *Context::AllocateBlockMetaData(AppPC start_pc) {
  auto meta = AllocateEmptyBlockMetaData();
  auto module_meta = MetaDataCast<ModuleMetaData *>(meta);
  auto module = module_manager.FindByAppPC(start_pc);
  module_meta->start_pc = start_pc;
  module_meta->source = module->OffsetOf(start_pc);
  return meta;
}

// Allocate and initialize some empty `BlockMetaData`.
BlockMetaData *Context::AllocateEmptyBlockMetaData(void) {
  return metadata_manager.Allocate();
}

// Register some meta-data with Granary.
void Context::RegisterMetaData(const MetaDataDescription *desc) {
  metadata_manager.Register(const_cast<MetaDataDescription *>(desc));
}

// Compile some code into one of the code caches.
void Context::Compile(LocalControlFlowGraph *cfg) {
  granary::Compile(cfg, &edge_code_cache);
}

// Allocate instances of the tools that will be used to instrument blocks.
Tool *Context::AllocateTools(void) {
  return tool_manager.AllocateTools();
}

// Free the allocated tools.
void Context::FreeTools(Tool *tools) {
  tool_manager.FreeTools(tools);
}

// Allocate a new code cache.
//
// Note: This should be a lightweight operation as it is usually invoked
//       whilst fine-grained locks are held.
CodeCacheInterface *Context::AllocateCodeCache(void) {
  return new CodeCache();
}

// Flush an entire code cache.
//
// Note: This should be a lightweight operation as it is usually invoked
//       whilst fine-grained locks are held (e.g. schedule for the allocator
//       to be freed).
void Context::FlushCodeCache(CodeCacheInterface *cache) {
  // TODO(pag): Implement me!
  delete cache;  // TODO(pag): This isn't actually right!!!
  GRANARY_UNUSED(cache);
}


// Allocates a direct edge data structure, as well as the code needed to
// back the direct edge.
DirectEdge *Context::AllocateDirectEdge(const BlockMetaData *source_block,
                                        BlockMetaData *dest_block) {
  return new DirectEdge(
      this, source_block, dest_block,
      edge_code_cache.AllocateBlock(arch::EDGE_CODE_SIZE_BYTES));
}

}  // namespace granary
