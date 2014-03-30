/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_ITERATOR_H_
#define GRANARY_CFG_ITERATOR_H_

#include "granary/base/base.h"

namespace granary {

// Forward declarations.
class Instruction;
class NativeInstruction;
class BasicBlock;

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

// An forward iterator for the application instructions of a basic block.
class AppInstructionIterator {
 public:
  inline AppInstructionIterator(void)
      : instr(nullptr) {}

  explicit AppInstructionIterator(Instruction *instr_);

  inline AppInstructionIterator begin(void) const {
    return *this;
  }

  inline AppInstructionIterator end(void) const {
    return AppInstructionIterator();
  }

  inline bool operator!=(const AppInstructionIterator &that) const {
    return instr != that.instr;
  }

  inline NativeInstruction *operator*(void) const {
    return instr;
  }

  void operator++(void);

 private:
  NativeInstruction *instr;
};

class BackwardAppInstructionIterator {
 public:
  inline BackwardAppInstructionIterator(void)
      : instr(nullptr) {}

  explicit BackwardAppInstructionIterator(Instruction *instr_);

  inline BackwardAppInstructionIterator begin(void) const {
    return *this;
  }

  inline BackwardAppInstructionIterator end(void) const {
    return BackwardAppInstructionIterator();
  }

  inline bool operator!=(const BackwardAppInstructionIterator &that) const {
    return instr != that.instr;
  }

  inline NativeInstruction *operator*(void) const {
    return instr;
  }

  void operator++(void);

 private:
  NativeInstruction *instr;
};

// An iterator for basic blocks that implements C++11 range-based for loops.
class BasicBlockIterator {
 public:
  GRANARY_INTERNAL_DEFINITION
  inline explicit BasicBlockIterator(BasicBlock *block_)
      : cursor(block_) {}

  inline BasicBlockIterator begin(void) const {
    return *this;
  }

  inline BasicBlockIterator end(void) const {
    return BasicBlockIterator();
  }

  inline bool operator!=(const BasicBlockIterator &that) const {
    return cursor != that.cursor;
  }

  void operator++(void);
  BasicBlock *operator*(void) const;

 private:
  inline BasicBlockIterator(void)
      : cursor(nullptr) {}

  // Pointer into a CFG's block list.
  GRANARY_POINTER(BasicBlock) *cursor;
};

}  // namespace granary

#endif  // GRANARY_CFG_ITERATOR_H_
