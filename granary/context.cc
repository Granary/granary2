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

GRANARY_DEFINE_bool(profile_direct_edges, false,
    "Should all direct edge control-flow transfers be profiled before they "
    "are patched? The default is no.");

namespace granary {
namespace arch {

// Generates the direct edge entry code for getting onto a Granary private
// stack, disabling interrupts, etc.
//
// Note: This has an architecture-specific implementation.
extern void GenerateDirectEdgeEntryCode(CachePC edge);

// Generates the direct edge code for a given `DirectEdge` structure.
//
// Note: This has an architecture-specific implementation.
extern void GenerateDirectEdgeCode(DirectEdge *edge, CachePC edge_entry_code);

}  // namespace arch
namespace {

static CachePC CreateDirectEntryCode(CodeCache *edge_code_cache) {
  auto entry_code = edge_code_cache->AllocateBlock(arch::EDGE_CODE_SIZE_BYTES);
  CodeCacheTransaction transaction(edge_code_cache, entry_code,
                                   entry_code + arch::EDGE_CODE_SIZE_BYTES);
  arch::GenerateDirectEdgeEntryCode(entry_code);
  return entry_code;
}

}  // namespace

ContextInterface::ContextInterface(void) {}

Context::Context(void)
    : module_manager(this),
      metadata_manager(),
      tool_manager(this),
      edge_code_cache(FLAG_edge_cache_slab_size),
      direct_edge_entry_code(CreateDirectEntryCode(&edge_code_cache)),
      edge_list_lock(),
      patched_edge_list(nullptr),
      unpatched_edge_list(nullptr) {

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

namespace {

// Free a linked list of edges.
static void FreeEdgeList(DirectEdge *edge) {
  DirectEdge *next_edge = nullptr;
  for (; edge; edge = next_edge) {
    next_edge = edge->next;
    delete edge;
  }
}

}  // namespace

Context::~Context(void) {
  FreeEdgeList(patched_edge_list);
  FreeEdgeList(unpatched_edge_list);
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
DirectEdge *Context::AllocateDirectEdge(const BlockMetaData *source_block_meta,
                                        BlockMetaData *dest_block_meta) {
  auto edge_code = edge_code_cache.AllocateBlock(arch::EDGE_CODE_SIZE_BYTES);
  auto edge = new DirectEdge(this, source_block_meta,
                             dest_block_meta, edge_code);

  do {  // Generate a small stub of code specific to this `DirectEdge`.
    CodeCacheTransaction transaction(
        &edge_code_cache, edge_code,
        edge_code + arch::EDGE_CODE_SIZE_BYTES);
    arch::GenerateDirectEdgeCode(edge, direct_edge_entry_code);
  } while (0);

  do {  // Add the edge to the unpatched list.
    FineGrainedLocked locker(&edge_list_lock);
    edge->next = unpatched_edge_list;
    unpatched_edge_list = edge;
  } while (0);

  return edge;
}

}  // namespace granary
