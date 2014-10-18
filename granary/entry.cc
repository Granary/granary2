/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/option.h"

#include "granary/code/edge.h"

#include "granary/context.h"
#include "granary/translate.h"

GRANARY_DEFINE_bool(profile_direct_edges, false,
    "Should all direct edge control-flow transfers be profiled before they "
    "are patched? The default is `no`.\n"
    "\n"
    "Note: If this is enabled then it will likely incur substantial\n"
    "      overheads, especially for multi-threaded programs. This is\n"
    "      because there is one shared profile counter per edge data\n"
    "      structure.");

GRANARY_DEFINE_bool(unsafe_patch_edges, false,
    "Should Granary try to patch direct edges as soon as possible? This is "
    "unsafe because Granary will not enforce proper barriers or other "
    "architectural requirements to cross-modifying code, and as such, enabling "
    "this option can result in spurious faults.");

// TODO(pag): Add an option that says put edge code in for all blocks, even if
//            not needed.

// TODO(pag): Only do profiling on conditional edges?

namespace granary {
namespace arch {

// Patch a direct edge.
//
// Note: This function has an architecture-specific implementation.
extern bool TryAtomicPatchEdge(ContextInterface *context, DirectEdge *edge);

}  // namespace arch
namespace {

// Update the edge code to target the new block.
static void UpdateEdge(DirectEdge *edge, CachePC target_pc) {
  std::atomic_thread_fence(std::memory_order_acquire);
  if (!FLAG_profile_direct_edges) {
    edge->entry_target_pc = target_pc;
  }

  // TODO(pag): Might not yield correct behavior w.r.t. edge profiling
  //            increments in assembly routines on more relaxed memory
  //            models.
  edge->num_executions = 1;
  edge->exit_target_pc = target_pc;
  std::atomic_thread_fence(std::memory_order_release);
}

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

}  // namespace
extern "C" {

// Enter into Granary to begin the translation process for a direct edge.
void granary_enter_direct_edge(DirectEdge *edge, ContextInterface *context) {
  GRANARY_IF_KERNEL(GRANARY_ASSERT(OnGranaryStack()));

  auto meta = edge->dest_meta.exchange(nullptr, std::memory_order_seq_cst);
  if (GRANARY_UNLIKELY(!meta)) {
    // Some other thread beat us to trying to follow through on this edge. This
    // can happen if the arch-specific edge entry code does not ensure mutual
    // exclusion over edge translation.
    return;
  }
  UpdateEdge(edge, Translate(context, meta));

  if (GRANARY_UNLIKELY(FLAG_unsafe_patch_edges)) {
    arch::TryAtomicPatchEdge(context, edge);
    // TODO(pag): Test the return value to increment some stats.
  }
}

// Enter into Granary to begin the translation process for an indirect edge.
// This is special because we need to do a few things:
//      1) We need to make a compensation fragment that directly jumps to
//         `target_app_pc`.
//      2) We need to set up the compensation fragment such that the direct
//         jump has a default non-`REQUEST_LATER` materialization strategy.
//      3) We need to prepend the out-edge code to the resulting code (by
//         "instantiating" the out edge into a fragment).
void granary_enter_indirect_edge(IndirectEdge *edge, ContextInterface *context,
                                 AppPC target_app_pc) {
  Translate(context, edge, target_app_pc);
}
}  // extern C
}  // namespace granary
