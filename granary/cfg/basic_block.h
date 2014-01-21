/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_CFG_BASIC_BLOCK_H_
#define GRANARY_CFG_BASIC_BLOCK_H_

#include "granary/arch/base.h"
#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/new.h"
#include "granary/base/types.h"
#include "granary/base/type_traits.h"

namespace granary {

class Instruction;

GRANARY_DECLARE_CLASS_HEIRARCHY(
    BasicBlock,
    NativeBasicBlock,
    InstrumentedBasicBlock,
    CachedBasicBlock,
    InFlightBasicBlock,
    FutureBasicBlock,
    UnknownBasicBlock);

// Forward declarations.
class BasicBlock;
class CachedBasicBlock;
class InFlightBasicBlock;
class BasicBlockMetaData;
class ControlFlowGraph;
class AnnotationInstruction;
class InstructionDecoder;

namespace detail {

class SuccessorBlockFinder;

// C++11 range-based for loop-compatible iterator for successor basic blocks.
class SuccessorBlockIterator {
 public:
  ~SuccessorBlockIterator(void);

  bool operator!=(const SuccessorBlockIterator &that) const;
  BasicBlock *operator*(void);
  const SuccessorBlockIterator &operator++(void);

 private:
  friend class SuccessorBlockFinder;

  SuccessorBlockIterator(void) = delete;
  SuccessorBlockIterator(BasicBlock *block_, void *data_);

  // Block from which we want to find successors.
  BasicBlock *block;
  BasicBlock *next_block;

  // Abstract data pointer used to figure out the next basic block.
  void *data;
};

// A container that is used by range based for loops for getting successor
// block iterators from a basic block.
class SuccessorBlockFinder {
 public:
  inline ~SuccessorBlockFinder(void) {
    block = nullptr;
    data = nullptr;
  }

  inline SuccessorBlockIterator begin(void) {
    return SuccessorBlockIterator(block, data);
  }

  inline SuccessorBlockIterator end(void) const {
    return SuccessorBlockIterator(nullptr, nullptr);
  }

 private:
  friend class granary::BasicBlock;
  friend class granary::CachedBasicBlock;
  friend class granary::InFlightBasicBlock;

  SuccessorBlockFinder(void) = delete;
  inline SuccessorBlockFinder(BasicBlock *block_, void *data_)
      : block(block_),
        data(data_) {}

  // Block from which we want to find successors.
  BasicBlock *block;
  void *data;
};

}  // namespace detail

// Abstract basic block of instructions.
class BasicBlock {
 public:
  explicit BasicBlock(AppProgramCounter app_start_pc_);
  virtual ~BasicBlock(void) = default;

  virtual detail::SuccessorBlockFinder Successors(void);

  GRANARY_BASE_CLASS(BasicBlock)

  // Starting program counter of this basic block in the app and in the code
  // cache.
  const AppProgramCounter app_start_pc;

 private:
  friend class detail::SuccessorBlockIterator;

  // Given a pointer to some abstract data, find the next successor basic block
  // of the current basic block and advance the data pointer.
  virtual BasicBlock *FindNextSuccessor(void **data);

  GRANARY_DISALLOW_COPY_AND_ASSIGN(BasicBlock);
};

// An instrumented basic block, i.e. a basic block that has been instrumented,
// is in the process of being instrumented, or will (likely) be instrumented.
class InstrumentedBasicBlock : public BasicBlock {
 public:
  virtual ~InstrumentedBasicBlock(void) = default;

  template <
    typename MetaPointerT,
    typename EnableIf<IsPointer<MetaPointerT>::RESULT, int>::Type = 0
  >
  MetaPointerT Meta(void) {
    GRANARY_UNUSED(meta);
    GRANARY_UNUSED(entry_meta);
    // TODO(pag): Implement this.
    return nullptr;
  }

  GRANARY_DERIVED_CLASS_OF(BasicBlock, InstrumentedBasicBlock)

 protected:
  InstrumentedBasicBlock(AppProgramCounter app_start_pc_,
                         const BasicBlockMetaData *entry_meta_,
                         BasicBlockMetaData *meta_);

 private:
  InstrumentedBasicBlock(void) = delete;

  // The meta-data associated with this basic block. `entry_meta` points to
  // some interned meta-data that is valid on entry to this basic block. The
  // value of `meta` changes over time, based on the logical state of the basic
  // block. For example, an in-flight basic block's `meta` field points to a
  // copy of `entry_meta`. `meta` might end up changing as the basic block is
  // decoded, instrumented, and encoded. Eventually, `meta` is interned.
  const BasicBlockMetaData * const entry_meta;
  BasicBlockMetaData *meta;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstrumentedBasicBlock);
};

// A basic block that has already been committed to the code cache.
//
// Cached basic blocks are treated as being long-lived, in that they cannot be
// re-instrumented. One subtlety is that, when materializing blocks, if the
// next block is already cached, then we make a *copy* of it, so that we can
// modify the successors in place (e.g. to show that an existing basic block
// loops back to an in-flight basic block).
class CachedBasicBlock : public InstrumentedBasicBlock {
 public:
  CachedBasicBlock(AppProgramCounter app_start_pc_,
                   CacheProgramCounter cache_start_pc_,
                   const BasicBlockMetaData *entry_meta_,
                   BasicBlockMetaData *meta_,
                   std::atomic<BasicBlock *> *successors_);

  virtual ~CachedBasicBlock(void) = default;
  virtual detail::SuccessorBlockFinder Successors(void);

  GRANARY_DERIVED_CLASS_OF(BasicBlock, CachedBasicBlock)

  // TODO(pag): Change the allocator so that cached basic blocks from the same
  //            modules (executables, kernel modules, mmaps, DLLs) are all
  //            managed by the same memory pool.
  //
  //            Perhaps can use the special syntax of new, e.g.
  //                operator new(size_t, Module *)
  //            Then:
  //                new (module) CachedBasicBlock;
  GRANARY_DEFINE_NEW_ALLOCATOR(CachedBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1  // Pack these tightly together as once they are committed to
  });              // the cache they are read-only.

  const CacheProgramCounter cache_start_pc;

 private:
  CachedBasicBlock(void) = delete;
  virtual BasicBlock *FindNextSuccessor(void **data);

  // TODO(pag): Should we store predecessors? Storing predecessors would have
  //            the advantage that unloading a module would allow us to slightly
  //            more easily be able to find potential references to the module
  //            inside of (potentially inlined) indirect lookup tables.

  // Array of successor basic blocks, ending in a NULL pointer.
  std::atomic<BasicBlock *> *successors;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CachedBasicBlock);
};

// A basic block that has been decoded but not yet committed to the code cache.
class InFlightBasicBlock : public InstrumentedBasicBlock {
 public:
  InFlightBasicBlock(AppProgramCounter app_start_pc_,
                     BasicBlockMetaData *entry_meta_,
                     BasicBlockMetaData *meta_);

  // TODO(pag): Clean up the memory of the instruction list.
  virtual ~InFlightBasicBlock(void) = default;

  virtual detail::SuccessorBlockFinder Successors(void);

  GRANARY_DERIVED_CLASS_OF(BasicBlock, InFlightBasicBlock)
  GRANARY_DEFINE_NEW_ALLOCATOR(CachedBasicBlock, {
    SHARED = true,
    ALIGNMENT = GRANARY_ARCH_CACHE_LINE_SIZE  // Spread these out as they are
  });                                         // often read and written to.

 private:
  friend class InstructionDecoder;

  InFlightBasicBlock(void) = delete;
  virtual BasicBlock *FindNextSuccessor(void **data);

  AnnotationInstruction *first;
  AnnotationInstruction *last;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InFlightBasicBlock);
};

// A basic block that has not yet been decoded, and might eventually be decoded.
class FutureBasicBlock : public InstrumentedBasicBlock {
 public:
  FutureBasicBlock(AppProgramCounter app_start_pc_,
                   BasicBlockMetaData *entry_meta_);
  virtual ~FutureBasicBlock(void) = default;
  virtual detail::SuccessorBlockIterator begin(void);

  GRANARY_DERIVED_CLASS_OF(BasicBlock, FutureBasicBlock)
  GRANARY_DEFINE_NEW_ALLOCATOR(FutureBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1  // Read-only after allocation.
  });

 private:
  FutureBasicBlock(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(FutureBasicBlock);
};

// A basic block that has not yet been decoded, and which we don't know about
// at this time because it's the target of an indirect jump/call.
//
// Has a hidden implementation to prevent instantiation.
class UnknownBasicBlock;

// A native basic block, i.e. this points to either native code, or some stub
// code that leads to native code.
class NativeBasicBlock : public BasicBlock {
 public:
  using BasicBlock::BasicBlock;
  virtual ~NativeBasicBlock(void) = default;

  GRANARY_DERIVED_CLASS_OF(BasicBlock, NativeBasicBlock)

  // TODO(pag): Change the allocator to take in an argument to `new`.
  GRANARY_DEFINE_NEW_ALLOCATOR(NativeBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1  // Pack these tightly as they are read-only.
  });
};

}  // namespace granary

#endif  // GRANARY_CFG_BASIC_BLOCK_H_
