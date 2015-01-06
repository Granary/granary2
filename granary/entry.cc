/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/option.h"

#include "granary/code/edge.h"

#include "granary/app.h"
#include "granary/context.h"
#include "granary/translate.h"

GRANARY_DEFINE_bool(unsafe_patch_edges, false,
    "Should Granary try to patch direct edges as soon as possible? This is "
    "unsafe because Granary will not enforce proper barriers or other "
    "architectural requirements to cross-modifying code, and as such, enabling "
    "this option can result in spurious faults.");

// TODO(pag): Add an option that says put edge code in for all blocks, even if
//            not needed.

// TODO(pag): Only do profiling on conditional edges?

namespace granary {

extern ReaderWriterLock gExitGranaryLock;

namespace arch {

// Patch a direct edge.
//
// Note: This function has an architecture-specific implementation.
extern bool TryAtomicPatchEdge(DirectEdge *edge);

}  // namespace arch
namespace {

#if defined(GRANARY_WHERE_kernel) && defined(GRANARY_TARGET_debug)

extern "C" {
// Initialized by `os/*/kernel/module/slot.c`
extern void *granary_stack_begin;
extern void *granary_stack_end;
}  // extern C

// Check that we're executing from a Granary-specific stack.
static inline bool OnGranaryStack(void) {
  auto sp = __builtin_frame_address(0);
  return granary_stack_begin <= sp && sp < granary_stack_end;
}
#endif  // GRANARY_WHERE_kernel && GRANARY_WHERE_debug

// Returns true if this edge has already been translated.
static bool EdgeHasTranslation(const DirectEdge *edge) {
  const auto begin = edge->edge_code_pc;
  const auto end = begin + arch::DIRECT_EDGE_CODE_SIZE_BYTES;
  return edge->entry_target_pc < begin || edge->entry_target_pc >= end;
}

}  // namespace
extern "C" {

// Enter into Granary to begin the translation process for a direct edge.
GRANARY_ENTRYPOINT void granary_enter_direct_edge(DirectEdge *edge) {
  VALGRIND_ENABLE_ERROR_REPORTING; {
    GRANARY_IF_KERNEL(GRANARY_ASSERT(OnGranaryStack()));
    ReadLockedRegion exit_locker(&gExitGranaryLock);
    os::LockedRegion edge_locker(&edge->lock);
    if (!EdgeHasTranslation(edge)) {
      auto context = GlobalContext();
      edge->entry_target_pc = Translate(context, edge->dest_block_meta);
      edge->dest_block_meta = nullptr;
      if (!FLAG_unsafe_patch_edges || !arch::TryAtomicPatchEdge(edge)) {
        context->PreparePatchDirectEdge(edge);
      }
    }
  } VALGRIND_DISABLE_ERROR_REPORTING;
}

// Enter into Granary to begin the translation process for an indirect edge.
GRANARY_ENTRYPOINT void granary_enter_indirect_edge(IndirectEdge *edge,
                                                    AppPC target_app_pc) {
  VALGRIND_ENABLE_ERROR_REPORTING; {
    GRANARY_IF_KERNEL(GRANARY_ASSERT(OnGranaryStack()));
    ReadLockedRegion exit_locker(&gExitGranaryLock);
    os::LockedRegion edge_locker(&(edge->lock));
    auto &encoded_pc(edge->out_edges[target_app_pc]);
    if (!encoded_pc) {
      auto context = GlobalContext();
      auto meta = edge->dest_block_meta_template->Copy();
      auto app_meta = MetaDataCast<AppMetaData *>(meta);
      app_meta->start_pc = target_app_pc;
      encoded_pc = Translate(context, edge, meta);
      edge->out_edge_pc = encoded_pc;
    }
  } VALGRIND_DISABLE_ERROR_REPORTING;
}
}  // extern C
}  // namespace granary
