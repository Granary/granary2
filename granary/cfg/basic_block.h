/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_BASIC_BLOCK_H_
#define GRANARY_CFG_BASIC_BLOCK_H_

#include "granary/arch/base.h"
#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/list.h"
#include "granary/base/new.h"
#include "granary/base/refcount.h"
#include "granary/base/types.h"
#include "granary/base/type_trait.h"
#include "granary/cfg/iterator.h"

namespace granary {

class Instruction;

// Forward declarations.
class BasicBlock;
class CachedBasicBlock;
class DecodedBasicBlock;
class GenericMetaData;
class LocalControlFlowGraph;
class ControlFlowInstruction;
class BlockFactory;
class BasicBlockIterator;

namespace detail {

class SuccessorBlockIterator;

// A successor of a basic block. A successor is a pair defined as a control-flow
// instruction and the basic block that it targets.
class BasicBlockSuccessor {
 public:
  BasicBlockSuccessor(void) = delete;
  BasicBlockSuccessor(const BasicBlockSuccessor &) = default;

  // Control-transfer instruction leading to the target basic block.
  //
  // `const`-qualified so that `cti` isn't unlinked from an instruction list
  // while the successors are being iterated.
  const ControlFlowInstruction * const cti;

  // The basic block targeted by `cti`.
  BasicBlock * const block;

 private:
  friend class LocalControlFlowGraph;
  friend class SuccessorBlockIterator;

  inline BasicBlockSuccessor(ControlFlowInstruction *cti_,
                             BasicBlock *block_)
      : cti(cti_),
        block(block_) {}

  GRANARY_DISALLOW_ASSIGN(BasicBlockSuccessor);
};

// Iterator to find the successors of a basic block.
class SuccessorBlockIterator {
 public:
  inline SuccessorBlockIterator begin(void) const {
    return *this;
  }

  inline SuccessorBlockIterator end(void) const {
    return SuccessorBlockIterator();
  }

  inline bool operator!=(const SuccessorBlockIterator &that) const {
    return cursor != that.cursor;
  }

  BasicBlockSuccessor operator*(void) const;
  void operator++(void);

 private:
  friend class granary::BasicBlock;
  friend class granary::DecodedBasicBlock;

  inline SuccessorBlockIterator(void)
      : cursor(nullptr) {}

  GRANARY_INTERNAL_DEFINITION
  inline explicit SuccessorBlockIterator(Instruction *instr_)
      : cursor(instr_) {}

  // The next instruction that we will look at for
  GRANARY_POINTER(Instruction) *cursor;
};

}  // namespace detail

// Abstract basic block of instructions.
class BasicBlock : protected UnownedCountedObject {
 public:
  virtual ~BasicBlock(void) = default;

  GRANARY_INTERNAL_DEFINITION BasicBlock(void);

  // Find the successors of this basic block. This can be used as follows:
  //
  //    for(auto succ : block->Successors()) {
  //      succ.block
  //      succ.cti
  //    }
  //
  // Note: This method is only usefully defined for `DecodedBasicBlock`. All
  //       other basic block types are treated as having no successors.
  virtual detail::SuccessorBlockIterator Successors(void) const;

  // Returns the starting PC of this basic block in the (native) application.
  virtual AppPC StartAppPC(void) const = 0;

  // Returns the starting PC of this basic block in the (instrumented) code
  // cache.
  virtual CachePC StartCachePC(void) const = 0;

  // Returns the number of predecessors of this basic block within the LCFG.
  int NumLocalPredecessors(void) const;

  GRANARY_DECLARE_BASE_CLASS(BasicBlock)

 private:
  friend class BasicBlockIterator;
  friend class ControlFlowInstruction;
  friend class LocalControlFlowGraph;
  friend class BlockFactory;

  GRANARY_IF_EXTERNAL( BasicBlock(void) = delete; )

  // Connects together lists of basic blocks in the LCFG.
  GRANARY_INTERNAL_DEFINITION ListHead list;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(BasicBlock);
};

// An instrumented basic block, i.e. a basic block that has been instrumented,
// is in the process of being instrumented, or will (likely) be instrumented.
class InstrumentedBasicBlock : public BasicBlock {
 public:
  GRANARY_INTERNAL_DEFINITION
  explicit InstrumentedBasicBlock(GenericMetaData *meta_);

  virtual ~InstrumentedBasicBlock(void);

  // Return this basic block's meta-data.
  GenericMetaData *MetaData(void);

  // Returns the starting PC of this basic block in the (native) application.
  virtual AppPC StartAppPC(void) const override;

  // Returns the starting PC of this basic block in the (instrumented) code
  // cache.
  virtual CachePC StartCachePC(void) const override;

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, InstrumentedBasicBlock)

 private:
  friend class BlockFactory;

  InstrumentedBasicBlock(void) = delete;

  // The meta-data associated with this basic block. Points to some (usually)
  // interned meta-data that is valid on entry to this basic block.
  GRANARY_INTERNAL_DEFINITION GenericMetaData *meta;
  GRANARY_INTERNAL_DEFINITION uint32_t cached_meta_hash;

  // The starting PC of this basic block, if any.
  GRANARY_INTERNAL_DEFINITION AppPC native_pc;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstrumentedBasicBlock);
};

// A basic block that has already been committed to the code cache.
class CachedBasicBlock final : public InstrumentedBasicBlock {
 public:
  virtual ~CachedBasicBlock(void) = default;

  GRANARY_INTERNAL_DEFINITION
  using InstrumentedBasicBlock::InstrumentedBasicBlock;

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, CachedBasicBlock)
  GRANARY_DEFINE_NEW_ALLOCATOR(CachedBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  CachedBasicBlock(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CachedBasicBlock);
};

// A basic block that has been decoded but not yet committed to the code cache.
class DecodedBasicBlock final : public InstrumentedBasicBlock {
 public:
  virtual ~DecodedBasicBlock(void) = default;

  GRANARY_INTERNAL_DEFINITION
  explicit DecodedBasicBlock(GenericMetaData *meta_);

  virtual detail::SuccessorBlockIterator Successors(void) const override;

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, DecodedBasicBlock)
  GRANARY_DEFINE_NEW_ALLOCATOR(DecodedBasicBlock, {
    SHARED = true,
    ALIGNMENT = GRANARY_ARCH_CACHE_LINE_SIZE
  })

  // Return the first instruction in the basic block.
  Instruction *FirstInstruction(void) const;

  // Return the last instruction in the basic block.
  Instruction *LastInstruction(void) const;

  // Return an iterator for the instructions of the block.
  ForwardInstructionIterator Instructions(void) const;

  // Return a reverse iterator for the instructions of the block.
  BackwardInstructionIterator ReversedInstructions(void) const;

  // Used to find the next scheduled decoded basic block. This field is only
  // updated at assembly time.
  GRANARY_INTERNAL_DEFINITION DecodedBasicBlock *next;

 private:
  friend class LocalControlFlowGraph;

  DecodedBasicBlock(void) = delete;

  GRANARY_INTERNAL_DEFINITION
  void FreeInstructionList(void);

  // List of instructions in this basic block. Basic blocks have sole ownership
  // over their instructions.
  GRANARY_INTERNAL_DEFINITION Instruction * const first;
  GRANARY_INTERNAL_DEFINITION Instruction * const last;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(DecodedBasicBlock);
};

// Forward declaration.
enum BlockRequestKind : uint8_t;

// A basic block that has not yet been decoded, and might eventually be decoded.
class DirectBasicBlock final : public InstrumentedBasicBlock {
 public:
  virtual ~DirectBasicBlock(void) = default;
  GRANARY_INTERNAL_DEFINITION DirectBasicBlock(GenericMetaData *meta_);

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, DirectBasicBlock)
  GRANARY_DEFINE_NEW_ALLOCATOR(DirectBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  friend class BlockFactory;

  DirectBasicBlock(void) = delete;

  GRANARY_INTERNAL_DEFINITION BasicBlock *materialized_block;
  GRANARY_INTERNAL_DEFINITION BlockRequestKind materialize_strategy;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(DirectBasicBlock);
};

// A basic block that has not yet been decoded, and which we don't know about
// at this time because it's the target of an indirect jump/call.
class IndirectBasicBlock final : public InstrumentedBasicBlock {
 public:
  virtual ~IndirectBasicBlock(void) = default;

  GRANARY_INTERNAL_DEFINITION
  using InstrumentedBasicBlock::InstrumentedBasicBlock;

  // Returns the starting PC of this basic block in the (native) application.
  virtual AppPC StartAppPC(void) const override;

  // Returns the starting PC of this basic block in the (instrumented) code
  // cache.
  virtual CachePC StartCachePC(void) const override;

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, IndirectBasicBlock)
  GRANARY_DEFINE_NEW_ALLOCATOR(IndirectBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  IndirectBasicBlock(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(IndirectBasicBlock);
};

// A basic block that has not yet been decoded, and which we don't know about
// at this time because it's the target of an indirect jump/call.
class ReturnBasicBlock final : public BasicBlock {
 public:
  GRANARY_INTERNAL_DEFINITION ReturnBasicBlock(void);
  virtual ~ReturnBasicBlock(void) = default;

  // Returns the starting PC of this basic block in the (native) application.
  virtual AppPC StartAppPC(void) const override;

  // Returns the starting PC of this basic block in the (instrumented) code
  // cache.
  virtual CachePC StartCachePC(void) const override;

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, ReturnBasicBlock)
  GRANARY_DEFINE_NEW_ALLOCATOR(ReturnBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  GRANARY_IF_EXTERNAL( ReturnBasicBlock(void) = delete; )
  GRANARY_DISALLOW_COPY_AND_ASSIGN(ReturnBasicBlock);
};

// A native basic block, i.e. this points to either native code, or some stub
// code that leads to native code.
class NativeBasicBlock final : public BasicBlock {
 public:
  virtual ~NativeBasicBlock(void) = default;

  GRANARY_INTERNAL_DEFINITION
  inline explicit NativeBasicBlock(AppPC native_pc_)
      : native_pc(native_pc_) {}

  // Returns the starting PC of this basic block in the (native) application.
  virtual AppPC StartAppPC(void) const override;

  // Returns the starting PC of this basic block in the (instrumented) code
  // cache.
  virtual CachePC StartCachePC(void) const override;

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, NativeBasicBlock)
  GRANARY_DEFINE_NEW_ALLOCATOR(NativeBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  NativeBasicBlock(void) = delete;

  GRANARY_INTERNAL_DEFINITION const AppPC native_pc;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(NativeBasicBlock);
};

}  // namespace granary

#endif  // GRANARY_CFG_BASIC_BLOCK_H_
