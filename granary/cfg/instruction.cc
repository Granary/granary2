/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/breakpoint.h"

namespace granary {

GRANARY_DECLARE_CLASS_HEIRARCHY(
    (Instruction, 2),
      (AnnotationInstruction, 2 * 3),
        (LabelInstruction, 2 * 3 * 5),
      (NativeInstruction, 2 * 7),
        (BranchInstruction, 2 * 7 * 11),
        (ControlFlowInstruction, 2 * 7 * 13),
          (ExceptionalControlFlowInstruction, 2 * 7 * 13 * 19))

GRANARY_DEFINE_BASE_CLASS(Instruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, AnnotationInstruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, LabelInstruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, NativeInstruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, BranchInstruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, ControlFlowInstruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, ExceptionalControlFlowInstruction)

GRANARY_IMPLEMENT_NEW_ALLOCATOR(LabelInstruction)

Instruction *Instruction::Next(void) {
  return list.GetNext(this);
}

Instruction *Instruction::Previous(void) {
  return list.GetPrevious(this);
}

// Get the transient, tool-specific instruction meta-data as a `uintptr_t`.
uintptr_t Instruction::MetaData(void) const {
  return transient_meta;
}

// Set the transient, tool-specific instruction meta-data as a `uintptr_t`.
void Instruction::SetMetaData(uintptr_t meta) {
  transient_meta = meta;
}

Instruction *Instruction::InsertBefore(Instruction *instr) {
  list.SetPrevious(this, instr);
  return instr;
}

Instruction *Instruction::InsertAfter(Instruction *instr) {
  list.SetNext(this, instr);
  return instr;
}

Instruction *Instruction::InsertBefore(std::unique_ptr<Instruction> instr) {
  return InsertBefore(instr.release());
}

Instruction *Instruction::InsertAfter(std::unique_ptr<Instruction> instr) {
  return InsertAfter(instr.release());
}

// Unlink an instruction from an instruction list.
std::unique_ptr<Instruction> Instruction::Unlink(Instruction *instr) {
  instr->list.Unlink();
  return std::unique_ptr<Instruction>(instr);
}

// Make it so that inserting an instruction before the designated first
// instruction actually changes the block's first instruction. This avoids the
// issue of maintaining a designated first instruction, whilst also avoiding the
// issue of multiple `InsertBefore`s putting instructions in the wrong order.
Instruction *AnnotationInstruction::InsertBefore(Instruction *instr) {
  if (GRANARY_UNLIKELY(IA_BEGIN_BASIC_BLOCK == annotation)) {
    auto new_first = new AnnotationInstruction(annotation, data);
    auto block_first_ptr = UnsafeCast<Instruction **>(data);
    this->Instruction::InsertBefore(new_first);
    *block_first_ptr = new_first;
    annotation = IA_NOOP;
    data = 0;
  }
  return this->Instruction::InsertBefore(instr);
}

// Make it so that inserting an instruction after the designated last
// instruction actually puts the instruction before the last instruction. In
// other cases this behaves as normal.

// Make it so that inserting an instruction after the designated last
// instruction actually changes the block's last instruction. This avoids the
// issue of maintaining a designated last instruction, whilst also avoiding the
// issue of multiple `InsertAfter`s putting instructions in the wrong order.
Instruction *AnnotationInstruction::InsertAfter(Instruction *instr) {
  if (GRANARY_UNLIKELY(IA_END_BASIC_BLOCK == annotation)) {
    auto new_last = new AnnotationInstruction(annotation, data);
    auto block_last_ptr = UnsafeCast<Instruction **>(data);
    this->Instruction::InsertAfter(new_last);
    *block_last_ptr = new_last;
    annotation = IA_NOOP;
    data = 0;
  }
  return this->Instruction::InsertAfter(instr);
}

// Returns true if this instruction is a label.
bool AnnotationInstruction::IsLabel(void) const {
  return IA_LABEL == annotation;
}

// Returns true if this instruction is targeted by any branches.
bool AnnotationInstruction::IsBranchTarget(void) const {
  return IA_LABEL == annotation && 0 != data;
}

// Returns true if this represents the beginning of a new logical instruction.
bool AnnotationInstruction::IsInstructionBoundary(void) const {
  return IA_BEGIN_LOGICAL_INSTRUCTION == annotation;
}

LabelInstruction::LabelInstruction(void)
    : AnnotationInstruction(IA_LABEL),
      fragment(nullptr) {}

NativeInstruction::NativeInstruction(const arch::Instruction *instruction_)
    : instruction(*instruction_),
      os_annotation(nullptr) {
  GRANARY_IF_DEBUG( instruction.note_create = __builtin_return_address(0); )
}

NativeInstruction::~NativeInstruction(void) {}

// Return the address in the native code from which this instruction was
// decoded.
AppPC NativeInstruction::DecodedPC(void) const {
  return instruction.DecodedPC();
}

// Get the length of the instruction.
int NativeInstruction::DecodedLength(void) const {
  return instruction.DecodedLength();
}

// Returns true if this instruction is essentially a no-op, i.e. it does
// nothing and has no observable side-effects.
bool NativeInstruction::IsNoOp(void) const {
  return instruction.IsNoOp();
}

bool NativeInstruction::ReadsConditionCodes(void) const {
  return instruction.ReadsFlags();
}

bool NativeInstruction::WritesConditionCodes(void) const {
  return instruction.WritesFlags();
}

bool NativeInstruction::IsFunctionCall(void) const {
  return instruction.IsFunctionCall();
}

bool NativeInstruction::IsFunctionReturn(void) const {
  return instruction.IsFunctionReturn();
}

bool NativeInstruction::IsInterruptCall(void) const {
  return instruction.IsInterruptCall();
}

bool NativeInstruction::IsInterruptReturn(void) const {
  return instruction.IsInterruptReturn();
}

bool NativeInstruction::IsSystemCall(void) const {
  return instruction.IsSystemCall();
}

bool NativeInstruction::IsSystemReturn(void) const {
  return instruction.IsSystemReturn();
}

bool NativeInstruction::IsJump(void) const {
  return instruction.IsJump();
}

bool NativeInstruction::IsUnconditionalJump(void) const {
  return instruction.IsUnconditionalJump();
}

bool NativeInstruction::IsConditionalJump(void) const {
  return instruction.IsConditionalJump();
}

bool NativeInstruction::HasIndirectTarget(void) const {
  return instruction.HasIndirectTarget();
}

bool NativeInstruction::IsAppInstruction(void) const {
  return nullptr != instruction.DecodedPC();
}

void NativeInstruction::MakeAppInstruction(PC decoded_pc) {
  instruction.SetDecodedPC(decoded_pc);
}

// Get the opcode name.
const char *NativeInstruction::OpCodeName(void) const {
  return instruction.OpCodeName();
}

// Get the instruction selection name.
const char *NativeInstruction::ISelName(void) const {
  return instruction.ISelName();
}

// Returns the names of the instruction prefixes on this instruction.
const char *NativeInstruction::PrefixNames(void) const {
  return instruction.PrefixNames();
}

// Invoke a function on every operand.
void NativeInstruction::ForEachOperandImpl(
    const std::function<void(Operand *)> &func) {
  instruction.ForEachOperand(func);
}

// Try to match and bind one or more operands from this instruction. Returns
// the number of operands matched, starting from the first operand.
size_t NativeInstruction::CountMatchedOperandsImpl(
    std::initializer_list<OperandMatcher> matchers) {
  return instruction.CountMatchedOperands(matchers);
}

BranchInstruction::BranchInstruction(const arch::Instruction *instruction_,
                                     AnnotationInstruction *target_)
    : NativeInstruction(instruction_),
      target(DynamicCast<LabelInstruction *>(target_)) {
  GRANARY_ASSERT(nullptr != target);
  GRANARY_IF_DEBUG( instruction.note_create = __builtin_return_address(0); )

  // Mark this label as being targeted by some instruction.
  target->data += 1;
}

// Return the targeted instruction of this branch.
LabelInstruction *BranchInstruction::TargetLabel(void) const {
  return target;
}

// Modify this branch to target a different label.
void BranchInstruction::SetTargetInstruction(LabelInstruction *label) {
  *target->DataPtr<uint64_t>() -= 1;
  *label->DataPtr<uint64_t>() += 1;
  target = label;
}

// Initialize a control-flow transfer instruction.
ControlFlowInstruction::ControlFlowInstruction(
    const arch::Instruction *instruction_, BasicBlock *target_)
      : NativeInstruction(instruction_),
        return_address(nullptr),
        target(target_) {
  GRANARY_IF_DEBUG( instruction.note_create = __builtin_return_address(0); )
}

// Destroy a control-flow transfer instruction.
ControlFlowInstruction::~ControlFlowInstruction(void) {
  target = nullptr;
}

// Special case that breaks some of our abstractions: sometimes we convert
// direct control-flow instructions into indirect ones, because the native
// targets are too far away. When we do that, we still want to pretend that
// these instructions (after conversion to having an indirect target operand)
// are still direct.
bool ControlFlowInstruction::HasIndirectTarget(void) const {
  if (this->NativeInstruction::HasIndirectTarget()) {
    if (auto native_target = DynamicCast<NativeBasicBlock *>(target)) {
      return nullptr == native_target->StartAppPC();
    } else if (IsA<CachedBasicBlock *>(target)) {
      return false;
    }
    return true;
  }
  return false;
}

// Return the target block of this CFI.
BasicBlock *ControlFlowInstruction::TargetBlock(void) const {
  return target;
}

// Change the target of a control-flow instruction. This can involve an
// ownership transfer of the targeted basic block.
void ControlFlowInstruction::ChangeTarget(BasicBlock *new_target) const {
  target = new_target;
}

ExceptionalControlFlowInstruction::ExceptionalControlFlowInstruction(
    const arch::Instruction *instruction_,
    const arch::Instruction *orig_instruction_,
    BasicBlock *exception_target_, AppPC emulation_pc_)
    : ControlFlowInstruction(instruction_, exception_target_),
      emulation_pc(emulation_pc_) {
  GRANARY_ASSERT(!orig_instruction_->WritesToStackPointer());

  memcpy(&orig_instruction, orig_instruction_, sizeof orig_instruction);
  used_regs.Visit(orig_instruction_);
}

}  // namespace granary
