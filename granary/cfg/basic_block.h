/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_BASIC_BLOCK_H_
#define GRANARY_CFG_BASIC_BLOCK_H_

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/list.h"
#include "granary/base/new.h"
#include "granary/base/pc.h"
#include "granary/base/type_trait.h"

#include "granary/cfg/iterator.h"

#ifdef GRANARY_INTERNAL
# include "granary/code/register.h"
#endif

namespace granary {

class Instruction;

// Forward declarations.
class BasicBlock;
class CachedBasicBlock;
class DecodedBasicBlock;
class BlockMetaData;
class LocalControlFlowGraph;
class ControlFlowInstruction;
class BlockFactory;
class BasicBlockIterator;
class ReverseBasicBlockIterator;

namespace detail {

class SuccessorBlockIterator;

// A successor of a basic block. A successor is a pair defined as a control-flow
// instruction and the basic block that it targets.
class BasicBlockSuccessor {
 public:
  BasicBlockSuccessor(void) = delete;
  BasicBlockSuccessor(const BasicBlockSuccessor &) = default;

  // Control-flow instruction leading to the target basic block.
  //
  // `const`-qualified so that `cfi` isn't unlinked from an instruction list
  // while the successors are being iterated.
  ControlFlowInstruction * const cfi;

  // The basic block targeted by `cfi`.
  BasicBlock * const block;

 private:
  friend class LocalControlFlowGraph;
  friend class SuccessorBlockIterator;

  inline BasicBlockSuccessor(ControlFlowInstruction *cfi_,
                             BasicBlock *block_)
      : cfi(cfi_),
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
class BasicBlock {
 public:
  virtual ~BasicBlock(void) = default;

  GRANARY_INTERNAL_DEFINITION BasicBlock(void);

  // Find the successors of this basic block. This can be used as follows:
  //
  //    for (auto succ : block->Successors()) {
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

  // Retunrs a unique ID for this basic block within the LCFG. This can be
  // useful for client tools to implement data flow passes.
  int Id(void) const;

  GRANARY_DECLARE_BASE_CLASS(BasicBlock)

 protected:
  template <typename> friend class ListOfListHead;
  friend class BasicBlockIterator;
  friend class ReverseBasicBlockIterator;
  friend class ControlFlowInstruction;
  friend class LocalControlFlowGraph;  // For `list` and `id`.
  friend class BlockFactory;

  GRANARY_IF_EXTERNAL( BasicBlock(void) = delete; )

  // Connects together lists of basic blocks in the LCFG.
  GRANARY_INTERNAL_DEFINITION ListHead list;

  // Unique ID for this block within its local control-flow graph. Defaults to
  // `-1` if the block does not belong to an LCFG.
  GRANARY_INTERNAL_DEFINITION int id;

  // The generation number for where this block can be materialized.
  GRANARY_INTERNAL_DEFINITION int generation;

  // Is this block reachable from the entry node of the LCFG?
  GRANARY_INTERNAL_DEFINITION bool is_reachable;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(BasicBlock);
};

// An instrumented basic block, i.e. a basic block that has been instrumented,
// is in the process of being instrumented, or will (likely) be instrumented.
class InstrumentedBasicBlock : public BasicBlock {
 public:
  GRANARY_INTERNAL_DEFINITION
  InstrumentedBasicBlock(LocalControlFlowGraph *cfg_, BlockMetaData *meta_);

  virtual ~InstrumentedBasicBlock(void);

  // Return this basic block's meta-data.
  virtual BlockMetaData *MetaData(void);

  // Return this basic block's meta-data.
  BlockMetaData *UnsafeMetaData(void);

  // Returns the starting PC of this basic block in the (native) application.
  virtual AppPC StartAppPC(void) const override;

  // Returns the starting PC of this basic block in the (instrumented) code
  // cache.
  virtual CachePC StartCachePC(void) const override;

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, InstrumentedBasicBlock)

 GRANARY_PROTECTED:

  // The local control-flow graph to which this block belongs.
  GRANARY_INTERNAL_DEFINITION LocalControlFlowGraph * const cfg;

  // The meta-data associated with this basic block. Points to some (usually)
  // interned meta-data that is valid on entry to this basic block.
  GRANARY_INTERNAL_DEFINITION BlockMetaData *meta;

 private:
  friend class BlockFactory;

  InstrumentedBasicBlock(void) = delete;

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
  GRANARY_DEFINE_INTERNAL_NEW_ALLOCATOR(CachedBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  CachedBasicBlock(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CachedBasicBlock);
};

// A basic block that has been decoded but not yet committed to the code cache.
class DecodedBasicBlock : public InstrumentedBasicBlock {
 public:
  virtual ~DecodedBasicBlock(void);

  GRANARY_INTERNAL_DEFINITION
  explicit DecodedBasicBlock(LocalControlFlowGraph *cfg_, BlockMetaData *meta_);

  // Return an iterator of the successor blocks of this basic block.
  virtual detail::SuccessorBlockIterator Successors(void) const override;

  // Allocates a new temporary virtual register for use by instructions within
  // this basic block.
  VirtualRegister AllocateVirtualRegister(int num_bytes=arch::GPR_WIDTH_BYTES);

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, DecodedBasicBlock)
  GRANARY_DEFINE_INTERNAL_NEW_ALLOCATOR(DecodedBasicBlock, {
    SHARED = true,
    ALIGNMENT = arch::CACHE_LINE_SIZE_BYTES
  })

  // Return the first instruction in the basic block.
  Instruction *FirstInstruction(void) const;

  // Return the last instruction in the basic block.
  Instruction *LastInstruction(void) const;

  // Return an iterator for the instructions of the block.
  InstructionIterator Instructions(void) const;

  // Return a reverse iterator for the instructions of the block.
  ReverseInstructionIterator ReversedInstructions(void) const;

  // Return an iterator for the application instructions of a basic block.
  AppInstructionIterator AppInstructions(void) const;

  // Return a reverse iterator for the application instructions of the block.
  ReverseAppInstructionIterator ReversedAppInstructions(void) const;

  // Add a new instruction to the beginning of the instruction list.
  void PrependInstruction(std::unique_ptr<Instruction> instr);

  // Add a new instruction to the end of the instruction list.
  void AppendInstruction(std::unique_ptr<Instruction> instr);

  // Add a new instruction to the beginning of the instruction list.
  GRANARY_INTERNAL_DEFINITION
  void UnsafePrependInstruction(Instruction *instr);

  // Add a new instruction to the end of the instruction list.
  GRANARY_INTERNAL_DEFINITION
  void UnsafeAppendInstruction(Instruction *instr);

 private:
  friend class BlockFactory;
  friend class LocalControlFlowGraph;

  DecodedBasicBlock(void) = delete;

  // List of instructions in this basic block. Basic blocks have sole ownership
  // over their instructions.
  //
  // Note: These fields are marked `GRANARY_CONST`, which is only externally
  //       resolved to `cont`, despite being internal-only fields. This is to
  //       document that they are effectively `const`, but that they can indeed
  //       change (e.g. `InsertBefore` and `InsertAfter` the first/last
  //       instructions).
  GRANARY_INTERNAL_DEFINITION Instruction * GRANARY_CONST first;
  GRANARY_INTERNAL_DEFINITION Instruction * GRANARY_CONST last;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(DecodedBasicBlock);
};

// Represents a decoded basic block that is meant as compensation code that
// points to an existing block.
class CompensationBasicBlock : public DecodedBasicBlock {
 public:
  GRANARY_INTERNAL_DEFINITION
  CompensationBasicBlock(LocalControlFlowGraph *cfg_,
                                BlockMetaData *meta_);


  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, CompensationBasicBlock)
  GRANARY_DEFINE_INTERNAL_NEW_ALLOCATOR(CompensationBasicBlock, {
    SHARED = true,
    ALIGNMENT = arch::CACHE_LINE_SIZE_BYTES
  })

 protected:
  friend class BlockFactory;

  // Should we be allowed to try to compare this block with another one?
  GRANARY_INTERNAL_DEFINITION bool is_comparable;
};

// Forward declaration.
enum BlockRequestKind : uint8_t;

// A basic block that has not yet been decoded, and might eventually be decoded.
class DirectBasicBlock final : public InstrumentedBasicBlock {
 public:
  virtual ~DirectBasicBlock(void);
  GRANARY_INTERNAL_DEFINITION DirectBasicBlock(
      LocalControlFlowGraph *cfg_, BlockMetaData *meta_,
      AppPC non_transparent_pc_=nullptr);

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, DirectBasicBlock)
  GRANARY_DEFINE_INTERNAL_NEW_ALLOCATOR(DirectBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  friend class BlockFactory;

  DirectBasicBlock(void) = delete;

  // How should we materialize this block, and if what block resulted from the
  // materialization?
  GRANARY_INTERNAL_DEFINITION BasicBlock *materialized_block;
  GRANARY_INTERNAL_DEFINITION BlockRequestKind materialize_strategy;

  // If we have something like a specialized return or an indirect jump/call to
  // a non-transparent code cache address (i.e. some PC in the code cache) then
  // we keep a record of that PC so that if the tool decides to materialize the
  // block into a native block then we can direct it to the `non_transparent_pc`
  // as opposed to the associated native PC, as that will most likely break
  // things.
  //
  // TODO(pag): This is a decision that is worth revisiting, as it is plausible
  //            that going to the native (transparent) address will work, at
  //            least up until the next implicit attach through a non-
  //            transparent (return) address.
  GRANARY_INTERNAL_DEFINITION AppPC non_transparent_pc;

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
  GRANARY_DEFINE_INTERNAL_NEW_ALLOCATOR(IndirectBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  IndirectBasicBlock(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(IndirectBasicBlock);
};

// A basic block that has not yet been decoded, and which we don't know about
// at this time because it's the target of an indirect jump/call.
class ReturnBasicBlock final : public InstrumentedBasicBlock {
 public:
  GRANARY_INTERNAL_DEFINITION ReturnBasicBlock(LocalControlFlowGraph *cfg_,
                                               BlockMetaData *meta_);
  virtual ~ReturnBasicBlock(void);

  // Returns true if this return basic block has meta-data. If it has meta-data
  // then the way that the branch is resolved is slightly more complicated.
  GRANARY_INTERNAL_DEFINITION inline bool UsesMetaData(void) const {
    return nullptr != meta;
  }

  // Return this basic block's meta-data. Accessing a return basic block's meta-
  // data will "create" it for the block.
  virtual BlockMetaData *MetaData(void) override;

  // Returns the starting PC of this basic block in the (native) application.
  virtual AppPC StartAppPC(void) const override;

  // Returns the starting PC of this basic block in the (instrumented) code
  // cache.
  virtual CachePC StartCachePC(void) const override;

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, ReturnBasicBlock)
  GRANARY_DEFINE_INTERNAL_NEW_ALLOCATOR(ReturnBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  ReturnBasicBlock(void) = delete;

  // The meta-data of this block, but where we only assign the `lazy_meta` to
  // `BasicBlock::meta` when a request of `MetaData` is made. This is so that
  // the default behavior is to not propagate meta-data through function
  // returns.
  GRANARY_INTERNAL_DEFINITION BlockMetaData *lazy_meta;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ReturnBasicBlock);
};

// A native basic block, i.e. this points to either native code, or some stub
// code that leads to native code.
class NativeBasicBlock final : public BasicBlock {
 public:
  virtual ~NativeBasicBlock(void) = default;

  GRANARY_INTERNAL_DEFINITION
  explicit NativeBasicBlock(AppPC native_pc_);

  // Returns the starting PC of this basic block in the (native) application.
  virtual AppPC StartAppPC(void) const override;

  // Returns the starting PC of this basic block in the (instrumented) code
  // cache.
  virtual CachePC StartCachePC(void) const override;

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, NativeBasicBlock)
  GRANARY_DEFINE_INTERNAL_NEW_ALLOCATOR(NativeBasicBlock, {
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
