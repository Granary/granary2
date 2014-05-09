/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

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
        (ControlFlowInstruction, 2 * 7 * 13))

GRANARY_DEFINE_BASE_CLASS(Instruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, AnnotationInstruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, LabelInstruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, NativeInstruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, BranchInstruction)
GRANARY_DEFINE_DERIVED_CLASS_OF(Instruction, ControlFlowInstruction)

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

Instruction *Instruction::InsertBefore(std::unique_ptr<Instruction> that) {
  Instruction *instr(that.release());
  list.SetPrevious(this, instr);
  return instr;
}

Instruction *Instruction::InsertAfter(std::unique_ptr<Instruction> that) {
  Instruction *instr(that.release());
  list.SetNext(this, instr);
  return instr;
}

// Unlink an instruction from an instruction list.
std::unique_ptr<Instruction> Instruction::Unlink(Instruction *instr) {
  GRANARY_ASSERT(!IsA<AnnotationInstruction *>(instr));
  instr->list.Unlink();

  // If we're unlinking a branch then make sure that the target itself does
  // not continue to reference the branch.
  auto branch = DynamicCast<BranchInstruction *>(instr);
  if (branch) {
    *(branch->TargetInstruction()->GetDataPtr<void *>()) = nullptr;
  }

  return std::unique_ptr<Instruction>(instr);
}

// Unlink an instruction in an unsafe way. The normal unlink process exists
// for ensuring some amount of safety, whereas this is meant to be used only
// in internal cases where Granary is safely doing an "unsafe" thing (e.g.
// when it's stealing instructions for `Fragment`s.
std::unique_ptr<Instruction> Instruction::UnsafeUnlink(void) {
  list.Unlink();
  return std::unique_ptr<Instruction>(this);
}

// Make it so that inserting an instruction before the designated first
// instruction actually changes the block's first instruction. This avoids the
// issue of maintaining a designated first instruction, whilst also avoiding the
// issue of multiple `InsertBefore`s putting instructions in the wrong order.
Instruction *AnnotationInstruction::InsertBefore(
    std::unique_ptr<Instruction> that) {
  if (GRANARY_UNLIKELY(IA_BEGIN_BASIC_BLOCK == annotation)) {
    auto new_first = new AnnotationInstruction(annotation, data);
    auto block_first_ptr = UnsafeCast<Instruction **>(data);
    this->Instruction::UnsafeInsertBefore(new_first);
    *block_first_ptr = new_first;
    annotation = IA_NOOP;
    data = 0;
  }
  return this->Instruction::InsertBefore(std::move(that));
}

// Make it so that inserting an instruction after the designated last
// instruction actually puts the instruction before the last instruction. In
// other cases this behaves as normal.

// Make it so that inserting an instruction after the designated last
// instruction actually changes the block's last instruction. This avoids the
// issue of maintaining a designated last instruction, whilst also avoiding the
// issue of multiple `InsertAfter`s putting instructions in the wrong order.
Instruction *AnnotationInstruction::InsertAfter(
    std::unique_ptr<Instruction> that) {
  if (GRANARY_UNLIKELY(IA_END_BASIC_BLOCK == annotation)) {
    auto new_last = new AnnotationInstruction(annotation, data);
    auto block_last_ptr = UnsafeCast<Instruction **>(data);
    this->Instruction::UnsafeInsertAfter(new_last);
    *block_last_ptr = new_last;
    annotation = IA_NOOP;
    data = 0;
  }
  return this->Instruction::InsertAfter(std::move(that));
}

// Returns true if this instruction is a label.
bool AnnotationInstruction::IsLabel(void) const {
  return IA_LABEL == annotation;
}

// Returns true if this instruction is targeted by any branches.
bool AnnotationInstruction::IsBranchTarget(void) const {
  return IA_LABEL == annotation && 0 != data;
}

LabelInstruction::LabelInstruction(void)
    : AnnotationInstruction(IA_LABEL) {}

NativeInstruction::NativeInstruction(const arch::Instruction *instruction_)
    : instruction(*instruction_) {}

NativeInstruction::~NativeInstruction(void) {}

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

// Invoke a function on every operand.
void NativeInstruction::ForEachOperandImpl(
    const std::function<void(Operand *)> &func) {
  instruction.ForEachOperand(func);
}

// Try to match and bind one or more operands from this instruction. Returns
// the number of operands matched, starting from the first operand.
size_t NativeInstruction::CountMatchedOperandsImpl(
    std::initializer_list<OperandMatcher> &&matchers) {
  return instruction.CountMatchedOperands(std::move(matchers));
}

BranchInstruction::BranchInstruction(const arch::Instruction *instruction_,
                                     LabelInstruction *target_)
    : NativeInstruction(instruction_),
      target(target_) {

  // Mark this label as being targeted by some instruction.
  *target->GetDataPtr<uint64_t>() += 1;
}

// Return the targeted instruction of this branch.
LabelInstruction *BranchInstruction::TargetInstruction(void) const {
  return target;
}

// Initialize a control-flow transfer instruction.
ControlFlowInstruction::ControlFlowInstruction(
    const arch::Instruction *instruction_, BasicBlock *target_)
      : NativeInstruction(instruction_),
        target(target_) {
  target->Acquire();
}

// Destroy a control-flow transfer instruction.
ControlFlowInstruction::~ControlFlowInstruction(void) {
  target->Release();
  auto old_target = target;
  target = nullptr;

  // In some cases, instructions need to clean up after basic blocks. E.g.
  // a CTI is unlinked, never re-linked, and therefore goes out of scope, thus
  // deleting the instruction. If that CTI is the only link to a basic block,
  // then the associated block must also be destroyed.
  if (!old_target->list.IsAttached() && old_target->CanDestroy()) {
    delete old_target;
  }
}

// Return the target block of this CFI.
BasicBlock *ControlFlowInstruction::TargetBlock(void) const {
  return target;
}

// Change the target of a control-flow instruction. This can involve an
// ownership transfer of the targeted basic block.
void ControlFlowInstruction::ChangeTarget(BasicBlock *new_target) const {
  GRANARY_ASSERT(new_target->list.IsAttached());
  GRANARY_ASSERT(-1 != new_target->Id());
  auto old_target = target;
  new_target->Acquire();
  target = new_target;
  old_target->Release();
}

}  // namespace granary
