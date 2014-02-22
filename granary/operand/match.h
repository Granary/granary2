/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_OPERAND_MATCH_H_
#define GRANARY_OPERAND_MATCH_H_

#include "granary/base/base.h"

namespace granary {

// Forward declaration.
class NativeInstruction;
class Operand;

namespace detail {

// High-level operand actions. Underneath these high-level actions we can
// specialize to different types of reads and write with:
//
//    Read        -> Conditional Read (IsConditionalRead)
//    Write       -> Conditional Write (IsConditionalWrite)
//    Read/Write  -> Read and conditionally written (IsConditionalWrite)
//    Read/Write  -> Conditionally read, always written (IsConditionalRead)
//
// To prevent ambiguities when matching, e.g. attempting to match the same
// Read/Write operand with two separate match operands, we make Read/Write
// operands explicit, such that a Read(...) can't match against a Read/Write
// operand.
enum class OperandAction {
  ANY,
  READ,
  WRITE,
  READ_WRITE
};

// Generic operand matcher.
class OperandMatcher {
 public:  // TODO(pag): Remove public!
  Operand *operand;
  OperandAction match_action;

  // If `is_bound` is `true`, then we are looking for a structural match against
  // one of the driver operands. `match_action` matches the driver
  // operand's action, and the `Operand`s action that we're matching against
  // is ignored. For example, one can match a write to some bound operand, even
  // if the bound operand is originally a read.
  //
  // If `is_bound` is `false` then matching is based on type and action only.
  bool is_bound;
};

// Operand matcher for multiple arguments. Returns the number of matched
// arguments, starting from the first argument.
size_t TryMatchAndBindOperands(NativeInstruction *instr,
                               std::initializer_list<OperandMatcher>);

// Operand matcher for multiple arguments. Returns true if all arguments are
// matched.
static inline bool MatchAndBindOperands(
    NativeInstruction *instr, std::initializer_list<OperandMatcher> ops) {
  return ops.size() == TryMatchAndBindOperands(instr, ops);
}

}  // namespace detail
}  // namespace granary

#endif  // GRANARY_OPERAND_MATCH_H_
