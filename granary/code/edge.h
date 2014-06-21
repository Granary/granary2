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
  DirectEdge(const BlockMetaData *source_meta_, BlockMetaData *dest_meta_,
             CachePC edge_code_);

  ~DirectEdge(void);

  // On entry to an edge, this address is targeted by an indirect jump. This
  // allows an edge to go right to the resolved block if the block address is
  // known and profiling is enabled.
  CachePC entry_target;

  // On exit from an edge, this is the address targeted by an indirect jump. By
  // default, this has the same value is `edge_code`, and so if two threads
  // execute the edge code, then one will end up in a busy loop that increments
  // `num_executions`. Eventually, when the target block is resolved, this is
  // changed to be the `cache_pc` of the target block.
  CachePC exit_target;

  // The number of executions.
  uint32_t num_executions;

  // The number of times the execution counter overflowed.
  uint32_t num_execution_overflows;

  // Next direct edge in a chain of all direct edges.
  DirectEdge *next;

  // Meta-data associated with the predecessor block.
  const BlockMetaData * const source_meta;

  // Meta-data associated with the block that must be translated. If this is
  // null then it means that this block has either been translated, or is in
  // the process of being translated.
  //
  // If this is null then the meta-data must be looked up in the code cache
  // index.
  std::atomic<BlockMetaData *> dest_meta;

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

static_assert(0 == offsetof(DirectEdge, entry_target),
    "Field `DirectEdge::cached_target` must be at offset `0`, as assembly "
    "routines depend on this.");

static_assert(8 == offsetof(DirectEdge, exit_target),
    "Field `DirectEdge::exit_target` must be at offset `8`, as assembly "
    "routines depend on this.");

static_assert(16 == offsetof(DirectEdge, num_executions),
    "Field `DirectEdge::num_executions` must be at offset `16`, as assembly "
    "routines depend on this.");

static_assert(20 == offsetof(DirectEdge, num_execution_overflows),
    "Field `DirectEdge::num_execution_overflows` must be at offset `24`, "
    "as assembly routines depend on this.");

static_assert(arch::CACHE_LINE_SIZE_BYTES >= sizeof(DirectEdge),
    "The `DirectEdge` structure should fit into an individual cache line.");

// Used to resolve indirect control-flow tranfers between the code cache and
// Granary.
class IndirectEdge {
 public:
};

}  // namespace granary

#endif  // GRANARY_CODE_EDGE_H_
