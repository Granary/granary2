/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_FACTORY_H_
#define GRANARY_CFG_FACTORY_H_

#include "granary/base/base.h"
#include "granary/base/bloom_filter.h"
#include "granary/base/pc.h"

namespace granary {

// Forward declarations.
class ContextInterface;
class LocalControlFlowGraph;
class BasicBlock;
class DirectBasicBlock;
class BlockMetaData;
class HashFunction;

#ifdef GRANARY_INTERNAL
namespace driver {
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

  // Materialization request cannot be satisfied. This happens when we try to
  // materialize a block accross a module boundary.
  REQUEST_DENIED
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

  // Satisfy all materialization requests.
  GRANARY_INTERNAL_DEFINITION void MaterializeRequestedBlocks(void);

  // Returns true if there are any pending materialization requests.
  GRANARY_INTERNAL_DEFINITION
  inline bool HasPendingMaterializationRequest(void) const {
    return has_pending_request;
  }

  GRANARY_INTERNAL_DEFINITION
  // Materialize the initial basic block.
  void MaterializeInitialBlock(BlockMetaData *meta);

  // Create a new direct basic block. This block is left as un-owned and
  // will not appear in any iterators until some instruction takes ownership
  // of it. This can be achieved by targeting this newly created basic block
  // with a CTI.
  std::unique_ptr<DirectBasicBlock> Materialize(AppPC start_pc);

 private:
  BlockFactory(void) = delete;

#if 0
  // Convert an indirect call into a direct call that jumps to an intermediate
  // block that does an indirect jump. This exists so that the lookup process
  // for the indirect target is done after the stack size change, and so that
  // it can also be instrumented.
  GRANARY_INTERNAL_DEFINITION
  Instruction *MakeIndirectCall(Instruction *prev_instr,
                                Instruction *last_instr,
                                driver::Instruction *instr);
#endif

  // Convert a decoded instruction into the internal Granary instruction IR.
  GRANARY_INTERNAL_DEFINITION
  Instruction *MakeInstruction(Instruction *prev_instr,
                               Instruction *last_instr,
                               driver::Instruction *instr);

  // Add the fall-through instruction for a block.
  GRANARY_INTERNAL_DEFINITION
  void AddFallThroughInstruction(driver::InstructionDecoder *decoder,
                                 DecodedBasicBlock *block,
                                 Instruction *last_instr, AppPC pc);

  // Decode an instruction list starting at `pc` and link the decoded
  // instructions into the instruction list beginning with `instr`.
  GRANARY_INTERNAL_DEFINITION
  void DecodeInstructionList(DecodedBasicBlock *block);

  // Hash the meta data of all basic blocks.
  GRANARY_INTERNAL_DEFINITION void HashBlockMetaDatas(HashFunction *hasher);

  // Iterates through the blocks and tries to materialize `DirectBasicBlock`s.
  GRANARY_INTERNAL_DEFINITION bool MaterializeDirectBlocks(void);

  // Unlink old blocks from the control-flow graph by changing the targets of
  // CTIs going to now-materialized `DirectBasicBlock`s.
  GRANARY_INTERNAL_DEFINITION void RelinkCFIs(void);

  // Try to find an already materialized version of `exclude` within the LCFG.
  GRANARY_INTERNAL_DEFINITION
  BasicBlock *MaterializeFromLCFG(DirectBasicBlock *exclude);

  // Returns true if we can try to materialize this block.
  GRANARY_INTERNAL_DEFINITION
  bool CanMaterializeBlock(DirectBasicBlock *block);

  // Materialize a direct basic block.
  GRANARY_INTERNAL_DEFINITION
  bool MaterializeBlock(DirectBasicBlock *block);

  // Used for fast checking on whether or not a block already exists in the
  // LCFG.
  GRANARY_INTERNAL_DEFINITION BloomFilter<256> meta_data_filter;

  // Then environment in which we're decoding.
  GRANARY_INTERNAL_DEFINITION ContextInterface *context;

  // The LCFG into which blocks are materialized.
  GRANARY_INTERNAL_DEFINITION LocalControlFlowGraph *cfg;

  GRANARY_INTERNAL_DEFINITION bool has_pending_request;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(BlockFactory);
};

}  // namespace granary

#endif  // GRANARY_CFG_FACTORY_H_
