/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"

#include "granary/base/container.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/cfg/basic_block.h"

#include "granary/code/compile.h"
#include "granary/code/edge.h"
#include "granary/code/metadata.h"

#include "granary/breakpoint.h"
#include "granary/cache.h"
#include "granary/context.h"
#include "granary/index.h"

#include "os/module.h"

GRANARY_DEFINE_positive_int(block_cache_slab_size, 512,
    "The number of pages allocated at once to store basic block code. Each "
    "context maintains its own block code allocator. The default value is "
    "`512` pages per slab (2MB).");

GRANARY_DEFINE_positive_int(edge_cache_slab_size, 256,
    "The number of pages allocated at once to store edge code. Each "
    "context maintains its own edge code allocator. The default value is "
    "`256` pages per slab (1MB).");

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
namespace os {
extern Container<ModuleManager> global_module_manager;
}  // namespace os
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
  metadata_manager->Register<StackMetaData>();
}

// Create a module for a Granary code cache.
static os::Module *MakeCodeCacheMod(ContextInterface *context,
                                    const char *name) {
  return new os::Module(os::ModuleKind::GRANARY_CODE_CACHE, name, context);
}

}  // namespace

ContextInterface::~ContextInterface(void) {}

Context::Context(void)
    : metadata_manager(),
      tool_manager(this),
      block_code_cache_mod(MakeCodeCacheMod(this, "[block cache]")),
      block_code_cache(block_code_cache_mod, FLAG_block_cache_slab_size),
      edge_code_cache_mod(MakeCodeCacheMod(this, "[edge cache]")),
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
  os::global_module_manager->Register(block_code_cache_mod);
  os::global_module_manager->Register(edge_code_cache_mod);
}

// Initialize all tools from a comma-separated list of tools.
void Context::InitTools(const char *tool_names) {

  // Force register some tools that should get priority over all others.
  tool_manager.Register("transparent_returns");
  tool_manager.Register(GRANARY_IF_KERNEL_ELSE("kernel", "user"));

  // Register tools specified at the command-line.
  ForEachCommaSeparatedString<MAX_TOOL_NAME_LEN>(
      tool_names,
      [&] (const char *tool_name) {
        tool_manager.Register(tool_name);
      });

  // Initialize all tools. Tool initialization is typically where tools will
  // register their specific their block meta-data, therefore it is important
  // to initialize all tools before finalizing the meta-data manager.
  auto tools = tool_manager.AllocateTools();
  for (auto tool : ToolIterator(tools)) {
    tool->Init();
  }
  tool_manager.FreeTools(tools);
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
InstrumentationTool *Context::AllocateTools(void) {
  return tool_manager.AllocateTools();
}

// Free the allocated tools.
void Context::FreeTools(InstrumentationTool *tools) {
  tool_manager.FreeTools(tools);
}

// Allocates a direct edge data structure, as well as the code needed to
// back the direct edge.
DirectEdge *Context::AllocateDirectEdge(BlockMetaData *dest_block_meta) {
  GRANARY_ASSERT(dest_block_meta->manager == &metadata_manager);

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
  GRANARY_ASSERT(dest_block_meta->manager == &metadata_manager);

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

namespace {
static std::atomic<ContextInterface *> context(ATOMIC_VAR_INIT(nullptr));
}

// Changes the active context.
void SetGlobalContext(ContextInterface *context_) {
  context.store(context_);
}

// Loads the active context.
ContextInterface *GlobalContext(void) {
  return context.load();
}

}  // namespace granary
