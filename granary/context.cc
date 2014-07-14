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
#include "granary/index.h"

GRANARY_DEFINE_positive_int(block_cache_slab_size, 64,
    "The number of pages allocated at once to store basic block code. Each "
    "context maintains its own block code allocator. The default value is "
    "`64` pages per slab.");

GRANARY_DEFINE_positive_int(edge_cache_slab_size, 16,
    "The number of pages allocated at once to store edge code. Each "
    "context maintains its own edge code allocator. The default value is "
    "`16` pages per slab.");

namespace granary {
namespace arch {

// Generates the direct edge entry code for getting onto a Granary private
// stack, disabling interrupts, etc.
//
// This code takes a pointer to the context so that the code generated will
// be able to pass the context pointer directly to `granary::EnterGranary`.
// This allows us to avoid saving the context pointer in the `DirectEdge`.
//
// Note: This has an architecture-specific implementation.
extern void GenerateDirectEdgeEntryCode(ContextInterface *context,
                                        CachePC edge);

// Generates the direct edge code for a given `DirectEdge` structure.
//
// Note: This has an architecture-specific implementation.
extern void GenerateDirectEdgeCode(DirectEdge *edge, CachePC edge_entry_code);

// Generates the indirect edge entry code for getting onto a Granary private
// stack, disabling interrupts, etc.
//
// This code takes a pointer to the context so that the code generated will
// be able to pass the context pointer directly to `granary::EnterGranary`.
// This allows us to avoid saving the context pointer in the `IndirectEdge`.
//
// Note: This has an architecture-specific implementation.
extern void GenerateIndirectEdgeEntryCode(ContextInterface *context,
                                          CachePC edge);
}  // namespace arch
namespace {

static CachePC CreateDirectEntryCode(ContextInterface *context,
                                     CodeCache *edge_code_cache) {
  auto entry_code = edge_code_cache->AllocateBlock(
      arch::DIRECT_EDGE_CODE_SIZE_BYTES);
  CodeCacheTransaction transaction(
      edge_code_cache, entry_code,
      entry_code + arch::DIRECT_EDGE_CODE_SIZE_BYTES);
  arch::GenerateDirectEdgeEntryCode(context, entry_code);
  return entry_code;
}

static CachePC CreateIndirectEntryCode(ContextInterface *context,
                                       CodeCache *edge_code_cache) {
  auto entry_code = edge_code_cache->AllocateBlock(
      arch::INDIRECT_EDGE_CODE_SIZE_BYTES);
  CodeCacheTransaction transaction(
      edge_code_cache, entry_code,
      entry_code + arch::INDIRECT_EDGE_CODE_SIZE_BYTES);
  arch::GenerateIndirectEdgeEntryCode(context, entry_code);
  return entry_code;
}

// Register internal meta-data.
static void InitMetaData(MetaDataManager *metadata_manager) {
  metadata_manager->Register<AppMetaData>();
  metadata_manager->Register<CacheMetaData>();
  metadata_manager->Register<IndexMetaData>();
  metadata_manager->Register<LiveRegisterMetaData>();
  metadata_manager->Register<StackMetaData>();
}

// Create a module for a Granary code cache.
static Module *MakeCodeCacheMod(const char *name) {
  return new Module(ModuleKind::GRANARY_CODE_CACHE, name);
}

}  // namespace

ContextInterface::~ContextInterface(void) {}

Context::Context(void)
    : module_manager(this),
      metadata_manager(),
      tool_manager(this),
      block_code_cache_mod(MakeCodeCacheMod("[block cache]")),
      block_code_cache(block_code_cache_mod, FLAG_block_cache_slab_size),
      edge_code_cache_mod(MakeCodeCacheMod("[edge cache]")),
      edge_code_cache(edge_code_cache_mod, FLAG_edge_cache_slab_size),
      direct_edge_entry_code(CreateDirectEntryCode(this, &edge_code_cache)),
      indirect_edge_entry_code(CreateIndirectEntryCode(this, &edge_code_cache)),
      edge_list_lock(),
      patched_edge_list(nullptr),
      unpatched_edge_list(nullptr),
      indirect_edge_list_lock(),
      indirect_edge_list(nullptr),
      code_cache_index(new Index) {
  InitMetaData(&metadata_manager);

  // Tell this environment about all loaded modules.
  module_manager.Register(block_code_cache_mod);
  module_manager.Register(edge_code_cache_mod);
}

// Initialize all tools from a comma-separated list of tools.
void Context::InitTools(const char *tool_names) {
  ForEachCommaSeparatedString<MAX_TOOL_NAME_LEN>(
      tool_names,
      [&] (const char *tool_name) {
        tool_manager.Register(tool_name);
      });

  // Do a dummy allocation and free of all tools. Tools register meta-data
  // through their constructors and so this will get all tool+option-specific
  // meta-data registered.
  tool_manager.FreeTools(tool_manager.AllocateTools());
}

namespace {

// Free a linked list of edges.
template <typename EdgeT>
static void FreeEdgeList(EdgeT *edge) {
  EdgeT *next_edge = nullptr;
  for (; edge; edge = next_edge) {
    next_edge = edge->next;
    delete edge;
  }
}

}  // namespace

Context::~Context(void) {
  FreeEdgeList(patched_edge_list);
  FreeEdgeList(unpatched_edge_list);
  FreeEdgeList(indirect_edge_list);
}

// Returns a pointer to the module containing some program counter.
const Module *Context::FindModuleContainingPC(AppPC pc) {
  return module_manager.FindByAppPC(pc);
}

// Returns a pointer to the first module whose name matches `name`.
const Module *Context::FindModuleByName(const char *name) {
  return module_manager.FindByName(name);
}

// Returns an iterator to all currently loaded modules.
ConstModuleIterator Context::LoadedModules(void) const {
  return module_manager.Modules();
}

// Allocate and initialize some `BlockMetaData`. This will also set-up the
// `AppMetaData` within the `BlockMetaData`.
BlockMetaData *Context::AllocateBlockMetaData(AppPC start_pc) {
  auto meta = AllocateEmptyBlockMetaData();
  MetaDataCast<AppMetaData *>(meta)->start_pc = start_pc;
  return meta;
}

// Allocate and initialize some `BlockMetaData`, based on some existing
// meta-data `meta`.
BlockMetaData *Context::AllocateBlockMetaData(
    const BlockMetaData *meta_template, AppPC start_pc) {
  auto meta = meta_template->Copy();
  MetaDataCast<AppMetaData *>(meta)->start_pc = start_pc;
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

// Allocate instances of the tools that will be used to instrument blocks.
Tool *Context::AllocateTools(void) {
  return tool_manager.AllocateTools();
}

// Free the allocated tools.
void Context::FreeTools(Tool *tools) {
  tool_manager.FreeTools(tools);
}

// Allocates a direct edge data structure, as well as the code needed to
// back the direct edge.
DirectEdge *Context::AllocateDirectEdge(BlockMetaData *dest_block_meta) {
  auto edge_code = edge_code_cache.AllocateBlock(
      arch::DIRECT_EDGE_CODE_SIZE_BYTES);
  auto edge = new DirectEdge(dest_block_meta, edge_code);

  do {  // Generate a small stub of code specific to this `DirectEdge`.
    CodeCacheTransaction transaction(
        &edge_code_cache, edge_code,
        edge_code + arch::DIRECT_EDGE_CODE_SIZE_BYTES);
    arch::GenerateDirectEdgeCode(edge, direct_edge_entry_code);
  } while (0);

  do {  // Add the edge to the unpatched list.
    FineGrainedLocked locker(&edge_list_lock);
    edge->next = unpatched_edge_list;
    unpatched_edge_list = edge;
  } while (0);

  return edge;
}

// Allocates an indirect edge data structure.
IndirectEdge *Context::AllocateIndirectEdge(
    const BlockMetaData *dest_block_meta) {
  auto edge = new IndirectEdge(dest_block_meta, indirect_edge_entry_code);
  FineGrainedLocked locker(&indirect_edge_list_lock);
  edge->next = indirect_edge_list;
  indirect_edge_list = edge;
  return edge;
}

// Returns a pointer to the code cache that is used for allocating code for
// basic blocks.
CodeCache *Context::BlockCodeCache(void) {
  return &block_code_cache;
}

// Get a pointer to this context's code cache index.
LockedIndex *Context::CodeCacheIndex(void) {
  return &code_cache_index;
}

}  // namespace granary
