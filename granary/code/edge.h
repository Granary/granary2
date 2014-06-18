/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_EDGE_H_
#define GRANARY_CODE_EDGE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/base.h"

#include "granary/base/base.h"
#include "granary/base/new.h"
#include "granary/base/pc.h"

namespace granary {

// Forward declarations.
class BlockMetaData;
class ContextInterface;

// Used to resolve direct control-flow transfers between the code cache and
// Granary.
class DirectEdge {
 public:
  DirectEdge(ContextInterface *context_, const BlockMetaData *source_meta_,
             BlockMetaData *dest_meta_, CachePC edge_code_);

  // The code targeted by this edge code. Initially, this takes on the value
  // of some edge code, and if profiling of edges isn't enabled, then it later
  // matches up with the `cache_pc` field of `CacheMetaData`. If profiling is
  // enabled then it remains unchanged, and always points to edge code.
  CachePC cached_target;

  // The number of executions.
  uint32_t num_executions;

  // The number of times the execution counter overflowed.
  uint32_t num_execution_overflows;

  // The context to which this edge belongs.
  ContextInterface * const context;

  // Next direct edge in a chain of all direct edges.
  DirectEdge *next;

  // Meta-data associated with the block that must be translated.
  const BlockMetaData * const source_meta;
  BlockMetaData * const dest_meta;

  // The stub code in an edge code cache that is used to context switch
  // into Granary and find/decode/instrument the block associated with
  // `dest_meta`.
  const CachePC edge_code;

  // Instruction that is patched by this direct edge.
  CachePC patch_instruction;

  GRANARY_DEFINE_NEW_ALLOCATOR(DirectEdge, {
    SHARED = true,

    // We want this cache-line aligned because the `num_executions` and
    // `num_execution_overflows` are potentially operated on atomically.
    ALIGNMENT = arch::CACHE_LINE_SIZE_BYTES
  })

 private:
  DirectEdge(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(DirectEdge);
}  __attribute__((packed));

static_assert(0 == offsetof(DirectEdge, cached_target),
    "Field `DirectEdge::cached_target` must be at offset `0`, as assembly "
    "routines depend on this.");

static_assert(8 == offsetof(DirectEdge, num_executions),
    "Field `DirectEdge::cached_target` must be at offset `0`, as assembly "
    "routines depend on this.");

static_assert(12 == offsetof(DirectEdge, num_execution_overflows),
    "Field `DirectEdge::cached_target` must be at offset `0`, as assembly "
    "routines depend on this.");

static_assert(arch::CACHE_LINE_SIZE_BYTES >= sizeof(DirectEdge),
    "The `DirectEdge` structure should fit into an individual cache line.");

// Used to resolve indirect control-flow tranfers between the code cache and
// Granary.
class IndirectEdge {
 public:
};

}  // namespace granary

#endif  // GRANARY_CODE_EDGE_H_
