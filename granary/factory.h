/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_FACTORY_H_
#define GRANARY_FACTORY_H_

#include "granary/base/base.h"
#include "granary/base/bloom_filter.h"

namespace granary {

// Forward declarations.
class Environment;
class LocalControlFlowGraph;
class BasicBlock;
class DirectBasicBlock;
class GenericMetaData;
class HashFunction;

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
  REQUEST_NATIVE
};


// Basic block materializer.
class BlockFactory {
 public:

  // Initialize the materializer with an environment and a local control-flow
  // graph. The environment is needed for lookups in the code cache index, and
  // the LCFG is needed so that blocks can be added.
  GRANARY_INTERNAL_DEFINITION
  explicit BlockFactory(LocalControlFlowGraph *cfg_);

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
  void MaterializeInitialBlock(GenericMetaData *meta);

  // Create a new direct basic block. This block is left as un-owned and
  // will not appear in any iterators until some instruction takes ownership
  // of it. This can be achieved by targeting this newly created basic block
  // with a CTI.
  std::unique_ptr<DirectBasicBlock> Materialize(AppProgramCounter start_pc);

 private:
  BlockFactory(void) = delete;

  // Hash the meta data of all basic blocks.
  GRANARY_INTERNAL_DEFINITION void HashBlockMetaDatas(HashFunction *hasher);

  // Iterates through the blocks and tries to materialize `DirectBasicBlock`s.
  GRANARY_INTERNAL_DEFINITION void MaterializeDirectBlocks(void);

  // Unlink old blocks from the control-flow graph by changing the targets of
  // CTIs going to now-materialized `DirectBasicBlock`s.
  GRANARY_INTERNAL_DEFINITION void RelinkCFIs(void);

  // Try to find an already materialized version of `exclude` within the LCFG.
  GRANARY_INTERNAL_DEFINITION
  BasicBlock *MaterializeFromLCFG(DirectBasicBlock *exclude);

  // Materialize a direct basic block.
  GRANARY_INTERNAL_DEFINITION
  BasicBlock *MaterializeBlock(DirectBasicBlock *block);

  // Used for fast checking on whether or not a block already exists in the
  // LCFG.
  GRANARY_INTERNAL_DEFINITION BloomFilter<256> meta_data_filter;

  // The LCFG into which blocks are materialized.
  GRANARY_INTERNAL_DEFINITION LocalControlFlowGraph *cfg;

  GRANARY_INTERNAL_DEFINITION bool has_pending_request;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(BlockFactory);
};

}  // namespace granary

#endif  // GRANARY_FACTORY_H_
