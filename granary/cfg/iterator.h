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
class InstructionIterator {
 public:
  inline InstructionIterator(void)
      : instr(nullptr) {}

  explicit inline InstructionIterator(Instruction *instr_)
      : instr(instr_) {}

  inline InstructionIterator begin(void) const {
    return *this;
  }

  inline InstructionIterator end(void) const {
    return InstructionIterator();
  }

  inline bool operator!=(const InstructionIterator &that) const {
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
class ReverseInstructionIterator {
 public:
  inline ReverseInstructionIterator(void)
      : instr(nullptr) {}

  explicit inline ReverseInstructionIterator(Instruction *instr_)
      : instr(instr_) {}

  inline ReverseInstructionIterator begin(void) const {
    return *this;
  }

  inline ReverseInstructionIterator end(void) const {
    return ReverseInstructionIterator();
  }

  inline bool operator!=(const ReverseInstructionIterator &that) const {
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

class ReverseAppInstructionIterator {
 public:
  inline ReverseAppInstructionIterator(void)
      : instr(nullptr) {}

  explicit ReverseAppInstructionIterator(Instruction *instr_);

  inline ReverseAppInstructionIterator begin(void) const {
    return *this;
  }

  inline ReverseAppInstructionIterator end(void) const {
    return ReverseAppInstructionIterator();
  }

  inline bool operator!=(const ReverseAppInstructionIterator &that) const {
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

  GRANARY_POINTER(BasicBlock) *cursor;
};

// An iterator for basic blocks that implements C++11 range-based for loops.
class ReverseBasicBlockIterator {
 public:
  GRANARY_INTERNAL_DEFINITION
  inline explicit ReverseBasicBlockIterator(BasicBlock *block_)
      : cursor(block_) {}

  inline ReverseBasicBlockIterator begin(void) const {
    return *this;
  }

  inline ReverseBasicBlockIterator end(void) const {
    return ReverseBasicBlockIterator();
  }

  inline bool operator!=(const ReverseBasicBlockIterator &that) const {
    return cursor != that.cursor;
  }

  void operator++(void);
  BasicBlock *operator*(void) const;

 private:
  inline ReverseBasicBlockIterator(void)
      : cursor(nullptr) {}

  GRANARY_POINTER(BasicBlock) *cursor;
};

}  // namespace granary

#endif  // GRANARY_CFG_ITERATOR_H_
