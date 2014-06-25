/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/option.h"

#include "granary/code/edge.h"

#include "granary/context.h"
#include "granary/translate.h"

GRANARY_DEFINE_bool(profile_direct_edges, false,
    "Should all direct edge control-flow transfers be profiled before they "
    "are patched? The default is no. If this is enabled then it will likely "
    "incur substantial overheads.");

// TODO(pag): Add an option that says put edge code in for all blocks, even if
//            not needed.

namespace granary {
namespace {

// Update the edge code to target the new block.
static void UpdateEdge(DirectEdge *edge, CachePC target_pc) {
  std::atomic_thread_fence(std::memory_order_acquire);
  if (!FLAG_profile_direct_edges) {
    edge->entry_target = target_pc;
  } else {
    // Reset, just in case some threads were spinning on (and thus
    // incrementing) these values in the edge entry code.
    edge->num_executions = 1;
    edge->num_execution_overflows = 0;
  }
  edge->exit_target = target_pc;
  std::atomic_thread_fence(std::memory_order_release);
}

}  // namespace

// Enter into Granary to begin the translation process for a direct edge.
void EnterGranary(DirectEdge *edge, ContextInterface *context) {
  auto meta = edge->dest_meta.exchange(nullptr, std::memory_order_seq_cst);
  if (GRANARY_UNLIKELY(!meta)) {
    // Some other thread beat us to trying to follow through on this edge. This
    // can happen if the arch-specific edge entry code does not ensure mutual
    // exclusion over edge translation.
    return;
  }
  UpdateEdge(edge, Translate(context, meta));
}

}  // namespace granary

extern "C" {

// Convenient C name for `EnterGranary` above. This name is referenced by
// assembly files.
void granary_enter_direct_edge(void)
    __attribute__((alias ("_ZN7granary12EnterGranaryEPNS_10DirectEdge"
                          "EPNS_16ContextInterfaceE")));

}  // extern C
