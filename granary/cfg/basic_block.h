/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_CFG_BASIC_BLOCK_H_
#define GRANARY_CFG_BASIC_BLOCK_H_

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/type_traits.h"

namespace granary {

class Instruction;


GRANARY_DECLARE_CLASS_HEIRARCHY(
    BasicBlock,
    CachedBasicBlock,
    InFlightBasicBlock,
    FutureBasicBlock,
    UnknownBasicBlock);


class FutureBasicBlock;


// Abstract basic block of instructions.
//
//
class BasicBlock {
 public:
  virtual ~BasicBlock(void) = default;

  // Apply a function to every (known) successor of a basic block. Importantly,
  // the successors of a basic block might underestimate the true number of
  // successors that are possible.
  //
  // Note: It is not meaningful to apply VisitSuccessors to a
  //       `FutureBasicBlock` or a `UnknownBasicBlock`.
  template <typename ClosureType>
  typename RemoveReference<decltype(ClosureType(BasicBlock *))>::Type
  VisitSuccessors(ClosureType func) {

  }

  // Apply a function to every (known) predecessor of a basic block.
  // Importantly, the predecessors of a basic block might underestimate the
  // true number of predecessors that are possible.
  template <typename ClosureType>
  typename RemoveReference<decltype(ClosureType(BasicBlock *))>::Type
  VisitPredecessors(ClosureType func) {

  }

  GRANARY_BASE_CLASS(BasicBlock)

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(BasicBlock);
};


// A basic block that has already been committed to the code cache.
class CachedBasicBlock : public BasicBlock {
 public:
  virtual ~CachedBasicBlock(void) = default;

  GRANARY_DERIVED_CLASS_OF(BasicBlock, CachedBasicBlock)

 private:
  // If a basic block is committed to the cache, then it typically has either
  // one or two successors: one if there is a fall-through or direct jump to
  // another block, and two if the basic block ends in a conditional jump that
  // falls through to another basic block.
  BasicBlock *successors[2];

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CachedBasicBlock);
};


// A basic block that has been decoded but not yet committed to the code cache.
class InFlightBasicBlock : public BasicBlock {
 public:
  virtual ~InFlightBasicBlock(void) = default;

  GRANARY_DERIVED_CLASS_OF(BasicBlock, InFlightBasicBlock)

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(InFlightBasicBlock);
};


// A basic block that has not yet been decoded, and might eventually be decoded.
class FutureBasicBlock : public BasicBlock {
 public:
  virtual ~FutureBasicBlock(void) = default;

  GRANARY_DERIVED_CLASS_OF(BasicBlock, FutureBasicBlock)

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(FutureBasicBlock);
};


// A basic block that has not yet been decoded, and which we don't know about
// at this time because it's the target of an indirect jump/call.
class UnknownBasicBlock : public BasicBlock {
 public:
  virtual ~UnknownBasicBlock(void) = default;

  GRANARY_DERIVED_CLASS_OF(BasicBlock, UnknownBasicBlock)

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(UnknownBasicBlock);
};


}  // namespace granary

#endif  // GRANARY_CFG_BASIC_BLOCK_H_
