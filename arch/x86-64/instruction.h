/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef ARCH_X86_64_INSTRUCTION_H_
#define ARCH_X86_64_INSTRUCTION_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "arch/instruction.h"
#include "arch/x86-64/operand.h"

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
    MAX_NUM_EXPLICIT_OPERANDS = XED_ENCODER_OPERANDS_MAX,
    MAX_NUM_OPERANDS = 11
  };

  Instruction(void);
  Instruction(const Instruction &that);

  // Get the decoded length of this instruction.
  inline size_t DecodedLength(void) const {
    return decoded_length;
  }

  inline PC DecodedPC(void) const {
    return decoded_pc;
  }

  inline size_t EncodedLength(void) const {
   return encoded_length;
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
    ops[0].is_annotation_instr = false;
    ops[0].type = XED_ENCODER_OPERAND_TYPE_BRDISP;
  }

  // Set a branch target to be an annotation instruction.
  inline void SetBranchTarget(AnnotationInstruction *instr) {
    ops[0].annotation_instr = instr;
    ops[0].is_annotation_instr = true;
    ops[0].type = XED_ENCODER_OPERAND_TYPE_BRDISP;
  }

  inline bool IsFunctionCall(void) const {
    return XED_CATEGORY_CALL == category;
  }

  inline bool IsFunctionTailCall(void) const {
    return is_tail_call;
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
    return XED_CATEGORY_NOP == category || XED_CATEGORY_WIDENOP == category;
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
  //       returned, regardless of if Granary is instrumenting user space or
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

  // Get the instruction selection name.
  const char *ISelName(void) const;

  // Get the names of the prefixes.
  const char *PrefixNames(void) const;

  // Mark this instruction as not encodable.
  inline void DontEncode(void) {
    dont_encode = true;
  }

  // Will this instruction be encoded?
  inline bool WillBeEncoded(void) const {
    return !dont_encode;
  }

  // Apply a function on every operand.
  void ForEachOperand(const std::function<void(granary::Operand *)> &func);

  // Operand matcher for multiple arguments. Returns the number of matched
  // arguments, starting from the first argument.
  size_t CountMatchedOperands(std::initializer_list<OperandMatcher> matchers);

  // Does this instruction enable interrupts?
  inline bool EnablesInterrupts(void) const {
    return XED_ICLASS_STI == iclass;
  }

  // Does this instruction disable interrupts?
  inline bool DisablesInterrupts(void) const {
    return XED_ICLASS_CLI == iclass;
  }

  // Can this instruction change the interrupt status to either of enabled or
  // disabled?
  inline bool CanEnableOrDisableInterrupts(void) const {
    // `FWAIT` doesn't actually enable/disable interrupts, but it can cause
    // pending FP exceptions to be raised, so it is nicer if we don't
    // accidentally disable interrupts around the `FWAIT`.
    return XED_ICLASS_POPF == iclass || XED_ICLASS_WRMSR == iclass ||
           XED_ICLASS_FWAIT == iclass;
  }

  // Does this instruction perform an atomic read/modify/write?
  inline bool IsAtomic(void) const {
    return is_atomic;
  }

  // Returns the total number of operands.
  inline size_t NumOperands(void) const {
    return num_ops;
  }

  // Returns the total number of explicit operands.
  inline size_t NumExplicitOperands(void) const {
    return num_explicit_ops;
  }

  // Where was this instruction encoded/decoded. When debugging, it's helpful
  // to have the `decoded_pc` remain around, even through alterations to
  // an instruction (it combines nicely with `note_create` and `note_alter`).
  alignas(alignof(void *)) GRANARY_IF_DEBUG_ELSE(struct, union) {
    AppPC decoded_pc;
    CachePC encoded_pc;
  };

  // Instruction class. This roughly corresponds to an opcode.
  alignas(Operand) struct {
    xed_iclass_enum_t iclass;
    mutable xed_iform_enum_t iform;  // Original `iform` at decode time.
    mutable unsigned isel;  // Instruction selection at decode time.
    xed_category_enum_t category;

    // The effective operand width (in bits) at decode time, or 0 if unknown.
    uint16_t effective_operand_width;

    // Decoded length of this instruction, or 0 if it wasn't decoded.
    uint8_t decoded_length;
    uint8_t encoded_length;

    // Number of explicit and total operands.
    uint8_t num_explicit_ops;
    uint8_t num_ops;
  };

  // All operands that Granary can make sense of. This includes implicit and
  // suppressed operands. The order between these and those referenced via
  // `xed_inst_t` is maintained.
  Operand ops[MAX_NUM_OPERANDS];

  // Useful for debugging the creation location of an instruction and the
  // alteration location of an instruction.
  GRANARY_IF_DEBUG( void *note_create, *note_alter; )

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpacked"
  alignas(uint16_t) struct {
    // Instruction prefixes.
    //
    // TODO(pag): Remove branch hints? Might be needed for special non-
    //            control-flow instructions.
    bool has_prefix_rep:1;
    bool has_prefix_repne:1;
    bool has_prefix_lock:1;

    // Does/did this read or write to the stack pointer?
    mutable bool reads_from_stack_pointer:1;
    mutable bool writes_to_stack_pointer:1;
    mutable bool analyzed_stack_usage:1;

    // Is this an atomic operation?
    bool is_atomic:1;

    // Is this a register save/restore operation? This is an optimization for
    // virtual register usage.
    //
    // Note: This flag should *only* be set if both the save and restore are
    //       guaranteed to be within the same fragment. If no such guarantee
    //       holds then the VR system will *really* screw things up (e.g.
    //       optimizing the save but not the restore).
    //
    // Note: This affects the behavior of copy propagation and fragment-local
    //       register scheduling. Specifically, a save/restore instruction
    //       cannot be involved in copy propagation. Further, the fragment-
    //       local register scheduler normally delays the spill of a register
    //       until the beginning of a fragment, so as to re-use the stolen
    //       register. With save/restore instructions, the spiller/filler is
    //       greedy and always spills before and fills after.
    bool is_save_restore:1;

    // Can this instruction be removed? This comes up in cases like late
    // mangling (`1_mangle.cc`) and VR slot allocation (`9_allocate_slots.cc`)
    // interacting with indirect calls and jumps, where the mangler has
    // converted a native call/jump into an indirect call/jump because the
    // native target is too far away from the code cache.
    bool is_sticky:1;

    // Should this instruction appear to have no effect on the stack pointer,
    // even if it actually does?
    bool is_stack_blind:1;

    // Should we *not* encode this instruction? Sometimes it's useful to have
    // an instruction exist for its side-effect on the virtual register
    // schedule, but never actually be encoded.
    bool dont_encode:1;

    // Was this a function call that was converted into a jump?
    bool is_tail_call:1;

  } __attribute__((packed));

#pragma clang diagnostic pop
};

}  // namespace arch
}  // namespace granary

#endif  // ARCH_X86_64_INSTRUCTION_H_
