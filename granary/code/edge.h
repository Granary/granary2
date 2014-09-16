/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_EDGE_H_
#define GRANARY_CODE_EDGE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/lock.h"
#include "granary/base/new.h"
#include "granary/base/pc.h"

namespace granary {

// Forward declarations.
class BlockMetaData;
class ContextInterface;

// Used to resolve direct control-flow transfers between the code cache and
// Granary.
class alignas(alignof(void *)) DirectEdge {
 public:
  DirectEdge(BlockMetaData *dest_meta_, CachePC edge_code_);

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

  // The number of executions. The edge entrypoint assembly routine
  // atomically increments this value by `2` each time, but when the edge is
  // patched, the value is changed to `1`. Thus, once the edge is patched, the
  // value is always odd, even if it overflows.
  uint64_t num_executions;

  // Next direct edge in a chain of all direct edges.
  DirectEdge *next;

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
  CachePC patch_instruction_pc;

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

static_assert(arch::CACHE_LINE_SIZE_BYTES >= sizeof(DirectEdge),
    "The `DirectEdge` structure should fit into an individual cache line.");

// Used to resolve indirect control-flow tranfers between the code cache and
// Granary.
class IndirectEdge {
 public:
  IndirectEdge(const BlockMetaData *dest_meta_,
               CachePC indirect_edge_entrypoint);
  ~IndirectEdge(void);

  // The entrypoint to the in-edge code. The value changes as follows:
  //
  //    1)  At allocation time, the value of this pointer will Granary's
  //        indirect edge entrypoint.
  //    2)  At edge code compile time, the value is changed to be the address
  //        of the first instruction in the "miss" case of indirect edge
  //        lookup. This is achieved via an `IA_UPDATE_ENCODED_ADDRESS`
  //        annotation instruction. The "miss" code transfers control to
  //        the indirect edge entrypoint (1).
  //    3)  After the first edge lookup is performed, this value is changed
  //        to be the address of the instantiated out-edge template, that
  //        checks to see if the target pc of the indirect CFI matches with
  //        the target block of the template, and if so jumps to the block, and
  //        otherwise jumps to the next instantiated template (inductive case)
  //        or jumps to the "miss" code (2; base case), which transfers control
  //        to (1).
  CachePC out_edge_pc;
  SpinLock out_edge_pc_lock;

  // Meta-data template associated with targets of this indirect CFI.
  const BlockMetaData * const meta_template;

  // Next edge in a linked list of all indirect edges in some context.
  IndirectEdge *next;

  // Pointer to the beginning and end of some executable code that is used as
  // a template for out edges.
  //
  // Note: These pointers are updated at JIT-compile time via an annotation
  //       instruction using `IA_UPDATE_ENCODED_ADDRESS`.
  AppPC out_edge_template;

  GRANARY_DEFINE_NEW_ALLOCATOR(IndirectEdge, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  IndirectEdge(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(IndirectEdge);
};

static_assert(0 == offsetof(IndirectEdge, out_edge_pc),
    "Field `IndirectEdge::in_edge_pc` must be at offset `0`, as assembly "
    "routines depend on this.");

}  // namespace granary

#endif  // GRANARY_CODE_EDGE_H_
