/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_FACTORY_H_
#define GRANARY_CFG_FACTORY_H_

#include "granary/base/base.h"
#include "granary/base/list.h"
#include "granary/base/pc.h"

namespace granary {

// Forward declarations.
class Context;
class Trace;
class Block;
class DirectBlock;
class InstrumentedBlock;
class CompensationBlock;
class BlockMetaData;
class HashFunction;
class Instruction;

#ifdef GRANARY_INTERNAL
namespace arch {
class InstructionDecoder;
}  // namespace arch
#endif  // GRANARY_INTERNAL

// Strategy for materializing basic blocks. The number associated with each
// materialization strategy represents granularity. For example, of two
// materialization requests are submitted for the same `DirectBlock`, then
// the chosen strategy will be the minimum of the two requests strategies.
enum BlockRequestKind : uint8_t {

  // Don't materialize this basic block. This is the default.
  kRequestBlockLater = 0,

  // Internal request that looks for a block in either the code cache index or
  // in the trace, but does *not* decode any blocks. This internal request is
  // submitted for each `DirectBlock` in the trace when no other pending
  // requests are outstanding. This can result in extra compensation fragments
  // being added, and therefore a new invocation of
  // `Tool::InstrumentControlFlow`.
  GRANARY_IF_INTERNAL_(kRequestBlockFromIndexOrTraceOnly = 5 )

  // Materialize this basic block into an `DecodedBlock` if it hasn't
  // already been cached (at the time of lookup) and if we haven't already
  // materialized it into our local control-flow graph.
  kRequestBlockFromIndexOrTrace = 10,

  // Materialize this basic block into an `DecodedBlock` if it hasn't
  // already been materialized into the CFG.
  kRequestBlockFromTrace = 20,

  // Always materialize this block into an `DecodedBlock`, even if it's
  // indexed in the cache or if already in the `Trace`.
  kRequestBlockDecodeNow = 30,

  // Materialize to the native target.
  kRequestBlockExecuteNatively = 40,

  // Materialization request cannot be satisfied. In practice, this is useful
  // for when you want to prevent some other tool from requesting the block
  // during this instrumentation session (e.g. to guarantee certain code
  // layout).
  kRequestBlockInFuture = 50
};

// Basic block materializer.
class BlockFactory {
 public:
  // Initialize the materializer with an environment and a local control-flow
  // graph. The environment is needed for lookups in the code cache index, and
  // the trace is needed so that blocks can be added.
  GRANARY_INTERNAL_DEFINITION
  BlockFactory(Context *context_, Trace *cfg_);

  // Request that a block be materialized. This does nothing if the block is
  // not a `DirectBlock`.
  void RequestBlock(
      Block *block,
      BlockRequestKind strategy=kRequestBlockFromIndexOrTrace);

  // Request that a `block` be materialized according to strategy `strategy`.
  // If multiple requests are made, then the most fine-grained strategy is
  // chosen.
  void RequestBlock(
      DirectBlock *block,
      BlockRequestKind strategy=kRequestBlockFromIndexOrTrace);

  // Request a block from the code cache index. If an existing block can be
  // adapted, then we will use that.
  GRANARY_INTERNAL_DEFINITION
  InstrumentedBlock *RequestIndexedBlock(BlockMetaData **meta_ptr);

  // Request a block that is the target of an indirect control-flow instruction.
  // To provide maximum flexibility (e.g. allow selective going native of
  // targets), we generate a dummy compensation fragment that jumps to a direct
  // basic block with a default non-`kRequestBlockInFuture` materialization
  // strategy.
  GRANARY_INTERNAL_DEFINITION
  InstrumentedBlock *MaterializeIndirectEntryBlock(BlockMetaData *meta);

  // Satisfy all materialization requests.
  GRANARY_INTERNAL_DEFINITION void MaterializeRequestedBlocks(void);

  // Returns true if there are any pending materialization requests.
  GRANARY_INTERNAL_DEFINITION
  bool HasPendingMaterializationRequest(void) const;

  // Materialize the initial basic block.
  GRANARY_INTERNAL_DEFINITION
  DecodedBlock *MaterializeDirectEntryBlock(BlockMetaData *meta);

  // Try to request the initial entry block from the code cache index.
  GRANARY_INTERNAL_DEFINITION
  InstrumentedBlock *RequestDirectEntryBlock(BlockMetaData **meta);

  // Create a new direct basic block. This block is left as un-owned and
  // will not appear in any iterators until some instruction takes ownership
  // of it. This can be achieved by targeting this newly created basic block
  // with a CTI.
  DirectBlock *Materialize(AppPC start_pc);

  // Request that an empty basic block be created and added to the trace.
  CompensationBlock *MaterializeEmptyBlock(AppPC start_pc=nullptr);

  // Convert a decoded instruction into the internal Granary instruction IR.
  GRANARY_INTERNAL_DEFINITION
  NativeInstruction *MakeInstruction(arch::Instruction *instr,
                                     const arch::Instruction *orig_instr);

  // Remove blocks that are now unnecessary.
  GRANARY_INTERNAL_DEFINITION void RemoveUnreachableBlocks(void);

 private:
  BlockFactory(void) = delete;

  // Add the fall-through instruction for a block.
  GRANARY_INTERNAL_DEFINITION
  void AddFallThroughInstruction(DecodedBlock *block,
                                 Instruction *last_instr, AppPC pc);

  // Decode an instruction list starting at `pc` and link the decoded
  // instructions into the instruction list beginning with `instr`.
  GRANARY_INTERNAL_DEFINITION
  void DecodeInstructionList(DecodedBlock *block);

  // Iterates through the blocks and tries to materialize `DirectBlock`s.
  GRANARY_INTERNAL_DEFINITION bool MaterializeDirectBlocks(void);

  // Unlink old blocks from the control-flow graph by changing the targets of
  // CTIs going to now-materialized `DirectBlock`s.
  GRANARY_INTERNAL_DEFINITION void RelinkCFIs(void);

  // Try to find an already materialized version of `exclude` within the trace.
  GRANARY_INTERNAL_DEFINITION
  InstrumentedBlock *MaterializeFromTrace(DirectBlock *exclude);

  // Returns true if we can try to materialize this block.
  GRANARY_INTERNAL_DEFINITION
  bool CanMaterializeBlock(DirectBlock *block);

  // Materialize a direct basic block.
  GRANARY_INTERNAL_DEFINITION
  bool MaterializeBlock(DirectBlock *block);

  // Then environment in which we're decoding.
  GRANARY_INTERNAL_DEFINITION Context *context;

  // The trace into which blocks are materialized.
  GRANARY_INTERNAL_DEFINITION Trace *cfg;

  GRANARY_INTERNAL_DEFINITION bool has_pending_request;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(BlockFactory);
};

}  // namespace granary

#endif  // GRANARY_CFG_FACTORY_H_
