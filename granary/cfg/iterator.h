/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_ITERATOR_H_
#define GRANARY_CFG_ITERATOR_H_

#include "granary/base/base.h"

namespace granary {

// Forward declarations.
class Instruction;
class NativeInstruction;
class Block;

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
class BlockIterator {
 public:
  GRANARY_INTERNAL_DEFINITION
  inline explicit BlockIterator(Block *block_)
      : cursor(block_) {}

  inline BlockIterator begin(void) const {
    return *this;
  }

  inline BlockIterator end(void) const {
    return BlockIterator();
  }

  inline bool operator!=(const BlockIterator &that) const {
    return cursor != that.cursor;
  }

  void operator++(void);
  Block *operator*(void) const;

 private:
  inline BlockIterator(void)
      : cursor(nullptr) {}

  GRANARY_POINTER(Block) *cursor;
};

// An iterator for basic blocks that implements C++11 range-based for loops.
class ReverseBlockIterator {
 public:
  GRANARY_INTERNAL_DEFINITION
  inline explicit ReverseBlockIterator(Block *block_)
      : cursor(block_) {}

  inline ReverseBlockIterator begin(void) const {
    return *this;
  }

  inline ReverseBlockIterator end(void) const {
    return ReverseBlockIterator();
  }

  inline bool operator!=(const ReverseBlockIterator &that) const {
    return cursor != that.cursor;
  }

  void operator++(void);
  Block *operator*(void) const;

 private:
  inline ReverseBlockIterator(void)
      : cursor(nullptr) {}

  GRANARY_POINTER(Block) *cursor;
};

}  // namespace granary

#endif  // GRANARY_CFG_ITERATOR_H_
