/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"
#include "arch/context.h"

#include "granary/base/container.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/cfg/block.h"

#include "granary/code/compile.h"
#include "granary/code/edge.h"
#include "granary/code/inline_assembly.h"

#include "granary/breakpoint.h"
#include "granary/cache.h"
#include "granary/context.h"
#include "granary/index.h"
#include "granary/metadata.h"

#include "os/module.h"

namespace granary {
namespace arch {

// Generates the wrapper code for a context callback.
//
// Note: This has an architecture-specific implementation.
extern Callback *GenerateContextCallback(AppPC func_pc);

// Generates the wrapper code for an outline callback.
//
// Note: This has an architecture-specific implementation.
extern Callback *GenerateInlineCallback(InlineFunctionCall *call);

}  // namespace arch
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

// Unlink the block meta-data pointers in an a un/patched edge list.
template <typename EdgeT>
static void UnlinkEdgeList(EdgeT *edge) {
  EdgeT *next_edge = nullptr;
  for (; edge; edge = next_edge) {
    next_edge = edge->next_patchable;
    edge->dest_block_meta = nullptr;
  }
}

template <typename T>
static void FreeCallbacks(const T &callback_map) {
  for (auto cb : callback_map.Values()) {
    delete cb;
  }
}

}  // namespace

Context::Context(void)
    : edge_list_lock(),
      edge_list(nullptr),
      unpatched_edge_list(nullptr),
      patched_edge_list(nullptr),
      indirect_edge_list_lock(),
      indirect_edge_list(nullptr),
      context_callbacks_lock(),
      context_callbacks(),
      inline_callbacks_lock(),
      inline_callbacks() {}

Context::~Context(void) {
  UnlinkEdgeList(unpatched_edge_list);
  UnlinkEdgeList(patched_edge_list);
  FreeEdgeList(edge_list);
  FreeCallbacks(context_callbacks);
  FreeCallbacks(inline_callbacks);
}

// Allocates a direct edge data structure, as well as the code needed to
// back the direct edge.
DirectEdge *Context::AllocateDirectEdge(BlockMetaData *dest_block_meta) {
  SpinLockedRegion locker(&edge_list_lock);
  auto edge = new DirectEdge(dest_block_meta, edge_list);
  edge_list = edge;
  return edge;
}

// Prepare a direct edge for patching.
void Context::PreparePatchDirectEdge(DirectEdge *edge) {
  SpinLockedRegion locker(&edge_list_lock);
  edge->next_patchable = unpatched_edge_list;
  unpatched_edge_list = edge;
}

// Allocates an indirect edge data structure.
IndirectEdge *Context::AllocateIndirectEdge(
    const BlockMetaData *source_block_meta,
    const BlockMetaData *dest_block_meta) {
  auto edge = new IndirectEdge(source_block_meta, dest_block_meta);
  SpinLockedRegion locker(&indirect_edge_list_lock);
  edge->next = indirect_edge_list;
  indirect_edge_list = edge;
  return edge;
}

// Returns a pointer to the `CachePC` associated with the context-callable
// function at `func_addr`.
const arch::Callback *Context::ContextCallback(AppPC func_pc) {
  os::LockedRegion locker(&context_callbacks_lock);
  auto *&cb(context_callbacks[func_pc]);
  if (!cb) cb = arch::GenerateContextCallback(func_pc);
  return cb;
}

// Returns a pointer to the code cache code associated with some outline-
// callable function at `func_addr`.
const arch::Callback *Context::InlineCallback(InlineFunctionCall *call) {
  os::LockedRegion locker(&inline_callbacks_lock);
  auto &cb(inline_callbacks[call->target_app_pc]);
  if (!cb) cb = arch::GenerateInlineCallback(call);
  return cb;
}

namespace {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#pragma clang diagnostic ignored "-Wexit-time-destructors"
GRANARY_EARLY_GLOBAL static Container<Context> gContext;
#pragma clang diagnostic pop
}  // namespace

// Initializes a new active context.
void InitContext(void) {
  gContext.Construct();
}

// Destroys the active context.
void ExitContext(void) {
  gContext.Destroy();
}

// Loads the active context.
Context *GlobalContext(void) {
  return gContext.AddressOf();
}

}  // namespace granary
