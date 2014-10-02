/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_FACTORY_H_
#define GRANARY_CFG_FACTORY_H_

#include "granary/base/base.h"
#include "granary/base/list.h"
#include "granary/base/pc.h"

namespace granary {

// Forward declarations.
class ContextInterface;
class LocalControlFlowGraph;
class BasicBlock;
class DirectBasicBlock;
class InstrumentedBasicBlock;
class BlockMetaData;
class HashFunction;
class Instruction;

#ifdef GRANARY_INTERNAL
namespace arch {
class InstructionDecoder;
}
#endif  // GRANARY_INTERNAL

// Strategy for materlizing basic blocks. The number associated with each
// materialization strategy represents granularity. For example, of two
// materialization requests are submitted for the same `DirectBasicBlock`, then
// the chosen strategy will be the minimum of the two requests strategies.
enum BlockRequestKind : uint8_t {

  // Don't materialize this basic block. This is the default.
  REQUEST_LATER,

  // Materialize this basic block into an `DecodedBasicBlock` if it hasn't
  // already been cached (at the time of lookup) and if we haven't already
  // materialized it into our local control-flow graph.
  REQUEST_CHECK_INDEX_AND_LCFG,

  // Materialize this basic block into an `DecodedBasicBlock` if it hasn't
  // already been materialized into the CFG.
  REQUEST_CHECK_LCFG,

  // Always materialize this block into an `DecodedBasicBlock`, even if it's
  // indexed in the cache or if already in the `LocalControlFlowGraph`.
  REQUEST_NOW,

  // Materialize to the native target.
  REQUEST_NATIVE,

  // Materialization request cannot be satisfied. In practice, this is useful
  // for when you want to prevent some other tool from requesting the block
  // during this instrumentation session (e.g. to guarantee certain code
  // layout).
  REQUEST_DENIED

#ifdef GRANARY_INTERNAL
  // Internal request that looks for a block in either the code cache index or
  // in the LCFG, but does *not* decode any blocks. This internal request is
  // submitted for each `DirectBasicBlock` in the LCFG when no other pending
  // requests are outstanding. This can result in extra compensation fragments
  // being added, and therefore a new invocation of
  // `Tool::InstrumentControlFlow`.
  , REQUEST_CHECK_INDEX_AND_LCFG_ONLY
#endif  // GRANARY_INTERNAL
};


// Basic block materializer.
class BlockFactory {
 public:

  // Initialize the materializer with an environment and a local control-flow
  // graph. The environment is needed for lookups in the code cache index, and
  // the LCFG is needed so that blocks can be added.
  GRANARY_INTERNAL_DEFINITION
  explicit BlockFactory(ContextInterface *context_,
                        LocalControlFlowGraph *cfg_);

  // Request that a block be materialized. This does nothing if the block is
  // not a `DirectBasicBlock`.
  void RequestBlock(
      BasicBlock *block,
      BlockRequestKind strategy=REQUEST_CHECK_INDEX_AND_LCFG);

  // Request that a `block` be materialized according to strategy `strategy`.
  // If multiple requests are made, then the most fine-grained strategy is
  // chosen.
  void RequestBlock(
      DirectBasicBlock *block,
      BlockRequestKind strategy=REQUEST_CHECK_INDEX_AND_LCFG);

  // Request a block from the code cache index. If an existing block can be
  // adapted, then we will use that.
  GRANARY_INTERNAL_DEFINITION
  InstrumentedBasicBlock *RequestIndexedBlock(BlockMetaData **meta_ptr);

  // Request a block that is the target of an indirect control-flow instruction.
  // To provide maximum flexibility (e.g. allow selective going native of
  // targets), we generate a dummy compensation fragment that jumps to a direct
  // basic block with a default non-`REQUEST_LATER` materialization strategy.
  GRANARY_INTERNAL_DEFINITION
  InstrumentedBasicBlock *MaterializeIndirectEntryBlock(BlockMetaData *meta);

  // Satisfy all materialization requests.
  GRANARY_INTERNAL_DEFINITION void MaterializeRequestedBlocks(void);

  // Returns true if there are any pending materialization requests.
  GRANARY_INTERNAL_DEFINITION
  inline bool HasPendingMaterializationRequest(void) const {
    return has_pending_request;
  }

  // Materialize the initial basic block.
  GRANARY_INTERNAL_DEFINITION
  DecodedBasicBlock *MaterializeDirectEntryBlock(BlockMetaData *meta);

  // Try to request the initial entry block from the code cache index.
  GRANARY_INTERNAL_DEFINITION
  InstrumentedBasicBlock *RequestDirectEntryBlock(BlockMetaData **meta);

  // Create a new direct basic block. This block is left as un-owned and
  // will not appear in any iterators until some instruction takes ownership
  // of it. This can be achieved by targeting this newly created basic block
  // with a CTI.
  DirectBasicBlock *Materialize(AppPC start_pc);

  // Convert a decoded instruction into the internal Granary instruction IR.
  GRANARY_INTERNAL_DEFINITION
  NativeInstruction *MakeInstruction(arch::Instruction *instr,
                                     const arch::Instruction *orig_instr);

 private:
  BlockFactory(void) = delete;

  // Add the fall-through instruction for a block.
  GRANARY_INTERNAL_DEFINITION
  void AddFallThroughInstruction(DecodedBasicBlock *block,
                                 Instruction *last_instr, AppPC pc);

  // Decode an instruction list starting at `pc` and link the decoded
  // instructions into the instruction list beginning with `instr`.
  GRANARY_INTERNAL_DEFINITION
  void DecodeInstructionList(DecodedBasicBlock *block);

  // Iterates through the blocks and tries to materialize `DirectBasicBlock`s.
  GRANARY_INTERNAL_DEFINITION bool MaterializeDirectBlocks(void);

  // Unlink old blocks from the control-flow graph by changing the targets of
  // CTIs going to now-materialized `DirectBasicBlock`s.
  GRANARY_INTERNAL_DEFINITION void RelinkCFIs(void);

  // Remove blocks that are now unnecessary.
  GRANARY_INTERNAL_DEFINITION void RemoveOldBlocks(void);

  // Try to find an already materialized version of `exclude` within the LCFG.
  GRANARY_INTERNAL_DEFINITION
  InstrumentedBasicBlock *MaterializeFromLCFG(DirectBasicBlock *exclude);

  // Returns true if we can try to materialize this block.
  GRANARY_INTERNAL_DEFINITION
  bool CanMaterializeBlock(DirectBasicBlock *block);

  // Materialize a direct basic block.
  GRANARY_INTERNAL_DEFINITION
  bool MaterializeBlock(DirectBasicBlock *block);

  // Then environment in which we're decoding.
  GRANARY_INTERNAL_DEFINITION ContextInterface *context;

  // The LCFG into which blocks are materialized.
  GRANARY_INTERNAL_DEFINITION LocalControlFlowGraph *cfg;

  GRANARY_INTERNAL_DEFINITION bool has_pending_request;

  GRANARY_INTERNAL_DEFINITION int generation;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(BlockFactory);
};

}  // namespace granary

#endif  // GRANARY_CFG_FACTORY_H_
