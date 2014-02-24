/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"
#include "granary/code/allocate.h"

#include "granary/environment.h"
#include "granary/metadata.h"
#include "granary/module.h"

namespace granary {

// Allocate and initialize some `BlockMetaData`. This will also set-up the
// `ModuleMetaData` within the `BlockMetaData`.
BlockMetaData *Environment::AllocateBlockMetaData(AppPC start_pc) {
  auto meta = new BlockMetaData;
  auto module_meta = MetaDataCast<ModuleMetaData *>(meta);
  auto module = module_manager->FindModuleByPC(start_pc);
  module_meta->start_pc = start_pc;
  module_meta->source = module->OffsetOf(start_pc);
  return meta;
}

// Allocate and initialize some empty `BlockMetaData`.
BlockMetaData *Environment::AllocateEmptyBlockMetaData(void) {
  return new BlockMetaData;
}

// Allocate some edge code from the edge code cache.
CachePC Environment::AllocateEdgeCode(int num_bytes) {
  return edge_code_allocator->Allocate(GRANARY_ARCH_CACHE_LINE_SIZE, num_bytes);
}

}  // namespace granary
