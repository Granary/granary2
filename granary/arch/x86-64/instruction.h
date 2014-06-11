/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_X86_64_INSTRUCTION_H_
#define GRANARY_ARCH_X86_64_INSTRUCTION_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/instruction.h"
#include "granary/arch/x86-64/operand.h"

namespace granary {
namespace arch {

// Represents a high-level API to the XED encoder/decoder. This API represents
// instructions at the granularity of instruction classes, and supports
// de-selection of `xed_decoded_inst_t` to `Instruction` and selections
// of `Instruction` to `xed_encoder_request_t`.
//
// An interesting side-effect of using virtual register operands is that
// instructions have no real "length". That is, it is only when the virtual
// register pass has replaced everything and these IR instructions have been
// lowered into `xed_encoder_request_t` (and then to `xed_decoded_inst_t`) that
// the length of a particular instruction becomes meaningful.
class Instruction : public InstructionInterface {
 public:
  enum {
    MAX_NUM_EXPLICIT_OPS = XED_ENCODER_OPERANDS_MAX,
    MAX_NUM_OPS = 11
  };

  Instruction(void);
  Instruction(const Instruction &that);

  // Get the decoded length of this instruction.
  inline int DecodedLength(void) const {
    return static_cast<int>(decoded_length);
  }

  inline PC DecodedPC(void) const {
    return decoded_pc;
  }

  inline int EncodedLength(void) const {
   return static_cast<int>(encoded_length);
 }

  inline CachePC EncodedPC(void) const {
    return encoded_pc;
  }

  inline void SetDecodedPC(PC decoded_pc_) {
    decoded_pc = decoded_pc_;
  }

  inline void SetEncodedPC(CachePC encoded_pc_) {
    encoded_pc = encoded_pc_;
  }

  // Get the PC-relative branch target.
  inline PC BranchTargetPC(void) const {
    return ops[0].branch_target.as_pc;  // TODO(pag): CALL_/JMP_FAR
  }

  // Invoke a function on the branch target, where the branch target is treated
  // as a `granary::Operand`.
  void WithBranchTargetOperand(
      const std::function<void(granary::Operand *)> &func);

  // Set the PC-relative branch target.
  inline void SetBranchTarget(PC pc) {
    ops[0].branch_target.as_pc = pc;
  }

  inline bool IsFunctionCall(void) const {
    return XED_CATEGORY_CALL == category;
  }

  inline bool IsFunctionReturn(void) const {
    return XED_ICLASS_RET_FAR == iclass || XED_ICLASS_RET_NEAR == iclass;
  }

  inline bool IsInterruptCall(void) const {
    return XED_CATEGORY_INTERRUPT == category;
  }

  inline bool IsInterruptReturn(void) const {
    return XED_ICLASS_IRET == iclass || XED_ICLASS_IRETD == iclass ||
           XED_ICLASS_IRETQ == iclass;
  }

  inline bool IsSystemCall(void) const {
    return XED_CATEGORY_SYSCALL == category;
  }

  inline bool IsSystemReturn(void) const {
    return XED_CATEGORY_SYSRET == category;
  }

  inline bool IsConditionalJump(void) const {
    return XED_CATEGORY_COND_BR == category;
  }

  inline bool IsUnconditionalJump(void) const {
    return XED_CATEGORY_UNCOND_BR == category;
  }

  inline bool IsJump(void) const {
    return IsUnconditionalJump() || IsConditionalJump();
  }

  // Returns true if this instruction is a control-flow instruction with an
  // indirect target.
  bool HasIndirectTarget(void) const;

  inline bool IsNoOp(void) const {
    return XED_CATEGORY_NOP == category;
  }

  // Returns true if an instruction reads from the stack pointer.
  bool ReadsFromStackPointer(void) const;

  // Returns true if an instruction writes to the stack pointer.
  bool WritesToStackPointer(void) const;

  // Returns true if the instruction modifies the stack pointer by a constant
  // value, otherwise returns false.
  bool ShiftsStackPointer(void) const;

  // Returns the statically know amount by which an instruction shifts the
  // stack pointer.
  //
  // Note: This should only be used after early mangling, as it assumes an
  //       absence of `ENTER` and `LEAVE`.
  int StackPointerShiftAmount(void) const;

  // If this instruction computes an address that is below (or possibly below)
  // the current stack pointer, then this function returns an estimate on that
  // amount. The value returned is either negative or zero.
  //
  // Note: This should only be used after early mangling.
  //
  // Note: If a dynamic offset is computed (e.g. stack pointer + register), then
  //       an ABI-specific value is returned. For example, for OSes running on
  //       x86-64/amd64 architectures, the user space red zone amount (-128) is
  //       returned, regardless of if Granary+ is instrumenting user space or
  //       kernel code.
  int ComputedOffsetBelowStackPointer(void) const;

  // Returns true if an instruction reads the flags.
  bool ReadsFlags(void) const;

  // Returns true if an instruction writes to the flags.
  bool WritesFlags(void) const;

  // Analyze this instruction's use of the stack pointer.
  void AnalyzeStackUsage(void) const;

  // Get the opcode name.
  const char *OpCodeName(void) const;

  // Apply a function on every operand.
  void ForEachOperand(const std::function<void(granary::Operand *)> &func);

  // Operand matcher for multiple arguments. Returns the number of matched
  // arguments, starting from the first argument.
  size_t CountMatchedOperands(std::initializer_list<OperandMatcher> &&matchers);

  // Where was this instruction encoded/decoded.
  union {
    AppPC decoded_pc;
    uintptr_t decoded_addr;
    CachePC encoded_pc;
    uintptr_t encoded_addr;
  } __attribute__((packed));

  // Instruction class. This roughly corresponds to an opcode.
  xed_iclass_enum_t iclass:16;
  mutable xed_iform_enum_t iform:16;  // Original iform at decode time.
  xed_category_enum_t category:8;

  // Decoded length of this instruction, or 0 if it wasn't decoded.
  union {
    uint8_t decoded_length;
    uint8_t encoded_length;
  };

  // Instruction prefixes.
  //
  // TODO(pag): Remove branch hints? Might be needed for special non-
  //            control-flow instructions.
  bool has_prefix_rep:1;
  bool has_prefix_repne:1;
  bool has_prefix_lock:1;
  bool has_prefix_br_hint_taken:1;
  bool has_prefix_br_hint_not_taken:1;

  // Does/did this read or write to the stack pointer?
  mutable bool reads_from_stack_pointer:1;
  mutable bool writes_to_stack_pointer:1;
  mutable bool analyzed_stack_usage:1;

  // Is this an atomic operation?
  bool is_atomic:1;

  // Is this a register save/restore operation? This is an optimization for
  // virtual register usage.
  bool is_save_restore:1;

  // Can this instruction be removed?
  bool is_sticky;

  // Number of explicit operands.
  uint8_t num_explicit_ops:4;

  // The effective operand width (in bits) at decode time, or -1 if unknown.
  int16_t effective_operand_width;

  // All operands that Granary can make sense of. This includes implicit and
  // suppressed operands. The order between these and those referenced via
  // `xed_inst_t` is maintained.
  Operand ops[MAX_NUM_EXPLICIT_OPS];
} __attribute__((packed));

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_ARCH_X86_64_INSTRUCTION_H_
