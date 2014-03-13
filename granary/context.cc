/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/cfg/control_flow_graph.h"

#include "granary/cache.h"
#include "granary/code/assemble.h"
#include "granary/instrument.h"

#include "granary/context.h"
#include "granary/metadata.h"
#include "granary/module.h"
#include "granary/tool.h"

namespace granary {

ContextInterface::ContextInterface(void) {}

// Allocate and initialize some `BlockMetaData`. This will also set-up the
// `ModuleMetaData` within the `BlockMetaData`.
BlockMetaData *Context::AllocateBlockMetaData(AppPC start_pc) {
  auto meta = AllocateEmptyBlockMetaData();
  auto module_meta = MetaDataCast<ModuleMetaData *>(meta);
  auto module = module_manager->FindByAppPC(start_pc);
  module_meta->start_pc = start_pc;
  module_meta->source = module->OffsetOf(start_pc);
  return meta;
}

// Allocate and initialize some empty `BlockMetaData`.
BlockMetaData *Context::AllocateEmptyBlockMetaData(void) {
  return metadata_manager->Allocate();
}

// Allocate some edge code from the edge code cache.
CachePC Context::AllocateEdgeCode(int num_bytes) {
  return edge_code_cache->AllocateBlock(num_bytes);
}

// Register some meta-data with Granary.
void Context::RegisterMetaData(const MetaDataDescription *desc) {
  metadata_manager->Register(const_cast<MetaDataDescription *>(desc));
}

// Allocate instances of the tools that will be used to instrument blocks.
Tool *Context::AllocateTools(void) {
  return tool_manager->AllocateTools(this);
}

// Free the allocated tools.
void Context::FreeTools(Tool *tools) {
  tool_manager->FreeTools(tools);
}

// Flush an entire code cache.
//
// Note: This should be a lightweight operation as it is usually invoked
//       whilst fine-grained locks are held (e.g. schedule for the allocator
//       to be freed).
void Context::FlushCodeCache(CodeCacheInterface *cache) {
  // TODO(pag): Implement me!
  GRANARY_UNUSED(cache);
}

// Allocate a new code cache.
//
// Note: This should be a lightweight operation as it is usually invoked
//       whilst fine-grained locks are held.
CodeCacheInterface *Context::AllocateCodeCache(void) {
  return new CodeCache();
}

// Compile some code into one of the code caches.
void Context::Compile(BlockMetaData *meta) {
  LocalControlFlowGraph cfg;
  auto module_meta = MetaDataCast<ModuleMetaData *>(meta);

  // TODO(pag): Would need to put some kind of guard (e.g. r/w lock) around this
  //            function so that on "reads", we write to the code cache, on on
  //            "writes" we flush parts of the cache or kill allocators.
  //            --> This would be well suited towards a lock in the environment.

  Instrument(this, &cfg, meta);
  Assemble(this, module_meta->GetCodeCache(), &cfg);
}

}  // namespace granary
