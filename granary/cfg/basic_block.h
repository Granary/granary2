/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_CFG_BASIC_BLOCK_H_
#define GRANARY_CFG_BASIC_BLOCK_H_

#include "granary/arch/base.h"
#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/new.h"
#include "granary/base/refcount.h"
#include "granary/base/types.h"
#include "granary/base/type_traits.h"

namespace granary {

class Instruction;

// Forward declarations.
class BasicBlock;
class CachedBasicBlock;
class InFlightBasicBlock;
class GenericMetaData;
class ControlFlowGraph;
class ControlFlowInstruction;

namespace detail {

class BasicBlockList;
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
  friend class ControlFlowGraph;
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
  friend class granary::InFlightBasicBlock;

  inline SuccessorBlockIterator(void)
      : cursor(nullptr) {}

  GRANARY_INTERNAL_DEFINITION
  explicit SuccessorBlockIterator(Instruction *instr_);

  // The next instruction that we will look at for
  GRANARY_POINTER(Instruction) *cursor;
};

// Iterator that moves forward through a list of instructions.
class ForwardInstructionIterator {
 public:
  inline ForwardInstructionIterator(void)
      : instr(nullptr) {}

  explicit inline ForwardInstructionIterator(Instruction *instr_)
      : instr(instr_) {}

  inline ForwardInstructionIterator begin(void) const {
    return *this;
  }

  inline ForwardInstructionIterator end(void) const {
    return ForwardInstructionIterator();
  }

  inline bool operator!=(const ForwardInstructionIterator &that) const {
    return instr != that.instr;
  }

  inline Instruction *operator*(void) const {
    return instr;
  }

  void operator++(void);

 private:
  Instruction *instr;
};

// Iterator that moves backward through a list of instructions.
class BackwardInstructionIterator {
 public:
  inline BackwardInstructionIterator(void)
      : instr(nullptr) {}

  explicit inline BackwardInstructionIterator(Instruction *instr_)
      : instr(instr_) {}

  inline BackwardInstructionIterator begin(void) const {
    return *this;
  }

  inline BackwardInstructionIterator end(void) const {
    return BackwardInstructionIterator();
  }

  inline bool operator!=(const BackwardInstructionIterator &that) const {
    return instr != that.instr;
  }

  inline Instruction *operator*(void) const {
    return instr;
  }

  void operator++(void);

 private:
  Instruction *instr;
};

}  // namespace detail

// Abstract basic block of instructions.
class BasicBlock : public UnownedCountedObject {
 public:
  virtual ~BasicBlock(void) = default;

  GRANARY_INTERNAL_DEFINITION
  inline explicit BasicBlock(AppProgramCounter app_start_pc_)
      : UnownedCountedObject(),
        app_start_pc(app_start_pc_),
        list(nullptr) {}

  // Find the successors of this basic block. This can be used as follows:
  //
  //    for(auto succ : block->Successors()) {
  //      succ.block
  //      succ.cti
  //    }
  //
  // Note: This method is only usefully defined for `InFlightBasicBlock`. All
  //       other basic block types are treated as having no successors.
  virtual detail::SuccessorBlockIterator Successors(void) const;

  GRANARY_DECLARE_BASE_CLASS(BasicBlock)

  // Starting program counter of this basic block in the app and in the code
  // cache.
  const AppProgramCounter app_start_pc;

 GRANARY_PROTECTED:
  // All blocks are "owned" by a single basic block list.
  GRANARY_POINTER(detail::BasicBlockList) *list;

 private:
  friend class detail::BasicBlockList;
  friend class ControlFlowInstruction;
  friend class ControlFlowGraph;
  GRANARY_DISALLOW_COPY_AND_ASSIGN(BasicBlock);
};

// An instrumented basic block, i.e. a basic block that has been instrumented,
// is in the process of being instrumented, or will (likely) be instrumented.
class InstrumentedBasicBlock : public BasicBlock {
 public:
  virtual ~InstrumentedBasicBlock(void) = default;

  // Return this basic block's meta-data.
  GenericMetaData *MetaData(void);

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, InstrumentedBasicBlock)

 GRANARY_PROTECTED:
  GRANARY_INTERNAL_DEFINITION
  InstrumentedBasicBlock(AppProgramCounter app_start_pc_,
                         GenericMetaData *meta_);

 private:
  friend class ControlFlowGraph;

  // The meta-data associated with this basic block. Points to some (usually)
  // interned meta-data that is valid on entry to this basic block.
  GRANARY_POINTER(GenericMetaData) * const meta;

  InstrumentedBasicBlock(void) = delete;
  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstrumentedBasicBlock);
};

// A basic block that has already been committed to the code cache.
class CachedBasicBlock : public InstrumentedBasicBlock {
 public:
  GRANARY_INTERNAL_DEFINITION
  CachedBasicBlock(AppProgramCounter app_start_pc_,
                   CacheProgramCounter cache_start_pc_,
                   GenericMetaData *meta_);

  virtual ~CachedBasicBlock(void) = default;

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, CachedBasicBlock)
  GRANARY_DEFINE_NEW_ALLOCATOR(CachedBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1
  })

  const CacheProgramCounter cache_start_pc;

 private:
  CachedBasicBlock(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CachedBasicBlock);
};

// A basic block that has been decoded but not yet committed to the code cache.
class InFlightBasicBlock : public InstrumentedBasicBlock {
 public:
  virtual ~InFlightBasicBlock(void) = default;

  GRANARY_INTERNAL_DEFINITION
  InFlightBasicBlock(AppProgramCounter app_start_pc_,
                     GenericMetaData *meta_);

  virtual detail::SuccessorBlockIterator Successors(void) const;

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, InFlightBasicBlock)
  GRANARY_DEFINE_NEW_ALLOCATOR(InFlightBasicBlock, {
    SHARED = true,
    ALIGNMENT = GRANARY_ARCH_CACHE_LINE_SIZE
  })

  // Return the first instruction in the basic block.
  Instruction *FirstInstruction(void) const;

  // Return the last instruction in the basic block.
  Instruction *LastInstruction(void) const;

  // Return an iterator for the instructions of the block.
  detail::ForwardInstructionIterator Instructions(void) const;

  // Return a reverse iterator for the instructions of the block.
  detail::BackwardInstructionIterator ReversedInstructions(void) const;

 private:
  friend class ControlFlowGraph;

  InFlightBasicBlock(void) = delete;

  GRANARY_INTERNAL_DEFINITION
  void FreeInstructionList(void);

  // List of instructions in this basic block. Basic blocks have sole ownership
  // over their instructions.
  GRANARY_INTERNAL_DEFINITION Instruction * const first;
  GRANARY_INTERNAL_DEFINITION Instruction * const last;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(InFlightBasicBlock);
};

// A basic block that has not yet been decoded, and might eventually be decoded.
class FutureBasicBlock : public InstrumentedBasicBlock {
 public:
  virtual ~FutureBasicBlock(void) = default;

  GRANARY_INTERNAL_DEFINITION
  inline FutureBasicBlock(AppProgramCounter app_start_pc_,
                          GenericMetaData *meta_)
      : InstrumentedBasicBlock(app_start_pc_, meta_) {}

  // Mark this basic block as being able to be run natively.
  void EnableDirectReturn(void);
  void RunNatively(void);

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, FutureBasicBlock)
  GRANARY_DEFINE_NEW_ALLOCATOR(FutureBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  FutureBasicBlock(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(FutureBasicBlock);
};

// A basic block that has not yet been decoded, and which we don't know about
// at this time because it's the target of an indirect jump/call.
class UnknownBasicBlock : public InstrumentedBasicBlock {
 public:
  virtual ~UnknownBasicBlock(void) = default;

  GRANARY_INTERNAL_DEFINITION
  inline UnknownBasicBlock(void)
      : InstrumentedBasicBlock(nullptr, nullptr) {}

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, UnknownBasicBlock)
  GRANARY_DEFINE_NEW_ALLOCATOR(FutureBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(UnknownBasicBlock);
};

// A native basic block, i.e. this points to either native code, or some stub
// code that leads to native code.
class NativeBasicBlock : public BasicBlock {
 public:
  using BasicBlock::BasicBlock;
  virtual ~NativeBasicBlock(void) = default;

  GRANARY_DECLARE_DERIVED_CLASS_OF(BasicBlock, NativeBasicBlock)
  GRANARY_DEFINE_NEW_ALLOCATOR(NativeBasicBlock, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(NativeBasicBlock);
};

}  // namespace granary

#endif  // GRANARY_CFG_BASIC_BLOCK_H_
