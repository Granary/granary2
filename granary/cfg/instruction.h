/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CFG_INSTRUCTION_H_
#define GRANARY_CFG_INSTRUCTION_H_

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/list.h"
#include "granary/base/new.h"
#include "granary/base/pc.h"
#include "granary/base/type_trait.h"

#ifdef GRANARY_INTERNAL
# include "granary/arch/driver.h"
#endif

#include "granary/cfg/operand.h"

namespace granary {

// Forward declarations.
class BasicBlock;
class ControlFlowInstruction;
class BlockFactory;
class Operand;

// Represents an abstract instruction.
class Instruction {
 public:

  GRANARY_INTERNAL_DEFINITION
  inline Instruction(void)
      : list(),
        cache_pc(nullptr),
        transient_meta(0) {}

  virtual ~Instruction(void) = default;

  GRANARY_DECLARE_BASE_CLASS(Instruction)

  Instruction *Next(void);
  Instruction *Previous(void);

  // Return the encoded location of this instruction.
  GRANARY_INTERNAL_DEFINITION
  inline CachePC StartCachePC(void) const {
    return cache_pc;
  }

  // Change the cache program counter.
  GRANARY_INTERNAL_DEFINITION
  inline void SetStartCachePC(CachePC cache_pc_) {
    cache_pc = cache_pc_;
  }

  // Get the transient, tool-specific instruction meta-data as an arbitrary,
  // `uintptr_t`-sized type.
  template <
    typename T,
    typename EnableIf<!TypesAreEqual<T, uintptr_t>::RESULT>::Type=0
  >
  inline T MetaData(void) const {
    static_assert(sizeof(T) <= sizeof(uintptr_t),
        "Transient meta-data type is too big. Client tools can only store "
        "a pointer-sized object as meta-data inside of an instruction.");
    return UnsafeCast<T>(MetaData());
  }

  // Get the transient, tool-specific instruction meta-data as a `uintptr_t`.
  uintptr_t MetaData(void) const;

  // Set the transient, tool-specific instruction meta-data as an arbitrary,
  // `uintptr_t`-sized type.
  template <
    typename T,
    typename EnableIf<!TypesAreEqual<T, uintptr_t>::RESULT>::Type=0
  >
  inline void SetMetaData(T meta) {
    static_assert(sizeof(T) <= sizeof(uintptr_t),
        "Transient meta-data type is too big. Client tools can only store "
        "a pointer-sized object as meta-data inside of an instruction.");
    return SetMetaData(UnsafeCast<uintptr_t>(meta));
  }

  // Set the transient, tool-specific instruction meta-data as a `uintptr_t`.
  void SetMetaData(uintptr_t meta);

  // Clear out the meta-data. This should be done by tools using instruction-
  // specific meta-data before they instrument instructions.
  inline void ClearMetaData(void) {
    SetMetaData(0UL);
  }

  // Inserts an instruction before/after the current instruction. Returns an
  // (unowned) pointer to the inserted instruction.
  GRANARY_INTERNAL_DEFINITION
  inline Instruction *UnsafeInsertBefore(Instruction *instr) {
    return this->Instruction::InsertBefore(std::unique_ptr<Instruction>(instr));
  }

  GRANARY_INTERNAL_DEFINITION
  inline Instruction *UnsafeInsertAfter(Instruction *instr) {
    return this->Instruction::InsertAfter(std::unique_ptr<Instruction>(instr));
  }

  // Inserts an instruction before/after the current instruction. Returns an
  // (unowned) pointer to the inserted instruction.
  GRANARY_IF_DEBUG(virtual)
  Instruction *InsertBefore(std::unique_ptr<Instruction>);

  GRANARY_IF_DEBUG(virtual)
  Instruction *InsertAfter(std::unique_ptr<Instruction>);

  // Unlink an instruction from an instruction list.
  static std::unique_ptr<Instruction> Unlink(Instruction *);

  // Unlink an instruction in an unsafe way. The normal unlink process exists
  // for ensuring some amount of safety, whereas this is meant to be used only
  // in internal cases where Granary is safely doing an "unsafe" thing (e.g.
  // when it's stealing instructions for `Fragment`s.
  GRANARY_INTERNAL_DEFINITION
  std::unique_ptr<Instruction> UnsafeUnlink(void);

  // Used to put instructions into lists.
  GRANARY_INTERNAL_DEFINITION ListHead list;

 protected:
  // Where has this instruction been encoded?
  GRANARY_INTERNAL_DEFINITION CachePC cache_pc;

  // Transient, tool-specific meta-data stored in this instruction. The lifetime
  // of this meta-data is the length of a tool's instrumentation function (i.e. 
  // `InstrumentControlFlow`, `InstrumentBlocks`, or `InstrumentBlock`). This
  // meta-data is the backing storage for tools to temporarily attach small
  // amounts of data (e.g. pointers to data structures, bit sets of live regs)
  // to instructions for a short period of time. 
  GRANARY_INTERNAL_DEFINITION uintptr_t transient_meta;

 private:

  GRANARY_IF_EXTERNAL( Instruction(void) = delete; )
  GRANARY_DISALLOW_COPY_AND_ASSIGN(Instruction);
};

// Represents a basic list of instructions. These are internal typedefs,
// mostly used later during the code assembly process.
GRANARY_INTERNAL_DEFINITION
typedef ListOfListHead<Instruction> InstructionList;
GRANARY_INTERNAL_DEFINITION
typedef ListHeadIterator<Instruction> InstructionListIterator;
GRANARY_INTERNAL_DEFINITION
typedef ReverseListHeadIterator<Instruction> ReverseInstructionListIterator;

// Built-in annotations.
GRANARY_INTERNAL_DEFINITION
enum InstructionAnnotation {
  // Used when we "kill" off meaningful annotations but want to leave the
  // associated instructions around.
  IA_NOOP,

  // Dummy annotations representing the beginning and end of a given basic
  // block.
  IA_BEGIN_BASIC_BLOCK,
  IA_END_BASIC_BLOCK,

  // Represents an inline assembly instruction.
  IA_INLINE_ASSEMBLY,

  // Target of a branch instruction.
  IA_LABEL,

  // Marks the stack as changing to a valid or undefined stack pointer value.
  IA_UNDEFINED_STACK,
  IA_UNKNOWN_STACK,
  IA_VALID_STACK,

  // Represents the definition of some `SSANode`, used in later assembly stages
  // so that all nodes are owned by some `Fragment`.
  IA_SSA_NODE_DEF,

  // An "undefinition" of a node that appears in a compensating fragment.
  // See `granary/code/assemble/6_track_ssa_vars.cc`.
  IA_SSA_NODE_UNDEF
};

// An annotation instruction is an environment-specific and implementation-
// specific annotations for basic blocks. Some examples would be that some
// instructions might result in page faults within kernel code. Annotations
// are used to mark those boundaries (e.g. by having an annotation that begins
// a faultable sequence of instructions and an annotation that ends it).
// Annotation instructions should not be removed by instrumentation.
class AnnotationInstruction : public Instruction {
 public:
  virtual ~AnnotationInstruction(void) = default;

  GRANARY_INTERNAL_DEFINITION
  inline explicit AnnotationInstruction(InstructionAnnotation annotation_)
      : annotation(annotation_),
        data(0) {}

  GRANARY_INTERNAL_DEFINITION
  template <typename T>
  inline AnnotationInstruction(InstructionAnnotation annotation_,
                               T data_)
      : annotation(annotation_),
        data(UnsafeCast<uintptr_t>(data_)) {}

  virtual Instruction *InsertBefore(std::unique_ptr<Instruction>);
  virtual Instruction *InsertAfter(std::unique_ptr<Instruction>);

  // Returns true if this instruction is a label.
  bool IsLabel(void) const;

  // Returns true if this instruction is targeted by any branches.
  bool IsBranchTarget(void) const;

  GRANARY_INTERNAL_DEFINITION GRANARY_CONST InstructionAnnotation annotation;
  GRANARY_INTERNAL_DEFINITION GRANARY_CONST uintptr_t GRANARY_CONST data;

  GRANARY_DECLARE_DERIVED_CLASS_OF(Instruction, AnnotationInstruction)
  GRANARY_DEFINE_NEW_ALLOCATOR(AnnotationInstruction, {
    SHARED = true,
    ALIGNMENT = 1
  })

  GRANARY_INTERNAL_DEFINITION
  template <typename T>
  inline T GetData(void) const {
    return UnsafeCast<T>(data);
  }

  GRANARY_INTERNAL_DEFINITION
  template <typename T>
  inline T *GetDataPtr(void) const {
    return UnsafeCast<T *>(&data);
  }

 private:
  AnnotationInstruction(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(AnnotationInstruction);
};

// A label instruction. Just a specialized annotation instruction. Enforces at
// the type leven that local control-flow instructions (within a block) must
// target a label. This makes it easier to identify fragment heads down the
// line when doing register allocation and assembling.
class LabelInstruction final : public AnnotationInstruction {
 public:
  GRANARY_INTERNAL_DEFINITION LabelInstruction(void);

  GRANARY_DECLARE_DERIVED_CLASS_OF(Instruction, LabelInstruction)
  GRANARY_DEFINE_NEW_ALLOCATOR(AnnotationInstruction, {
    SHARED = true,
    ALIGNMENT = 1
  })
};

// An instruction containing an driver-specific decoded instruction.
class NativeInstruction : public Instruction {
 public:
  virtual ~NativeInstruction(void);

  GRANARY_INTERNAL_DEFINITION
  explicit NativeInstruction(const arch::Instruction *instruction_);

  // Get the decoded length of the instruction. This is independent from the
  // length of the encoded instruction, which could be wildly different as a
  // single decoded instruction might map to many encoded instructions. If the
  // instruction was not decoded then this returns 0.
  int DecodedLength(void) const;

  // Returns true if this instruction is essentially a no-op, i.e. it does
  // nothing and has no observable side-effects.
  bool IsNoOp(void) const;

  // Driver-specific implementations.
  bool ReadsConditionCodes(void) const;
  bool WritesConditionCodes(void) const;
  bool IsFunctionCall(void) const;
  bool IsFunctionReturn(void) const;
  bool IsInterruptCall(void) const;
  bool IsInterruptReturn(void) const;
  bool IsSystemCall(void) const;
  bool IsSystemReturn(void) const;
  bool IsJump(void) const;
  bool IsUnconditionalJump(void) const;
  bool IsConditionalJump(void) const;
  bool HasIndirectTarget(void) const;
  bool IsAppInstruction(void) const;
  GRANARY_INTERNAL_DEFINITION void MakeAppInstruction(PC decoded_pc);

  // Get the opcode name.
  const char *OpCodeName(void) const;

  // Try to match and bind one or more operands from this instruction.
  //
  // Note: Matches are attempted in order!
  template <typename... OperandMatchers>
  inline bool MatchOperands(OperandMatchers... matchers) {
    return sizeof...(matchers) == CountMatchedOperandsImpl({matchers...});
  }

  // Try to match and bind one or more operands from this instruction. Returns
  // the number of operands matched, starting from the first operand.
  template <typename... OperandMatchers>
  inline size_t CountMatchedOperands(OperandMatchers... matchers) {
    return CountMatchedOperandsImpl({matchers...});
  }

  // Invoke a function on every operand.
  template <typename FuncT>
  inline void ForEachOperand(FuncT func) {
    ForEachOperandImpl(std::cref(func));
  }

  GRANARY_DECLARE_DERIVED_CLASS_OF(Instruction, NativeInstruction)
  GRANARY_DEFINE_NEW_ALLOCATOR(NativeInstruction, {
    SHARED = true,
    ALIGNMENT = 1
  })

 GRANARY_ARCH_PUBLIC:
  GRANARY_INTERNAL_DEFINITION arch::Instruction instruction;

 private:
  friend class ControlFlowInstruction;

  // Invoke a function on every operand.
  void ForEachOperandImpl(const std::function<void(Operand *)> &func);

  NativeInstruction(void) = delete;

  // Try to match and bind one or more operands from this instruction. Returns
  // the number of operands matched, starting from the first operand.
  size_t CountMatchedOperandsImpl(
      std::initializer_list<OperandMatcher> &&matchers);

  GRANARY_DISALLOW_COPY_AND_ASSIGN(NativeInstruction);
};

// Represents a control-flow instruction that is local to a basic block, i.e.
// keeps control within the same basic block.
class BranchInstruction final : public NativeInstruction {
 public:
  virtual ~BranchInstruction(void) = default;

  GRANARY_INTERNAL_DEFINITION
  inline BranchInstruction(const arch::Instruction *instruction_,
                           LabelInstruction *target_)
      : NativeInstruction(instruction_),
        target(target_) {}

  // Return the targeted instruction of this branch.
  LabelInstruction *TargetInstruction(void) const;

  GRANARY_DECLARE_DERIVED_CLASS_OF(Instruction, BranchInstruction)
  GRANARY_DEFINE_NEW_ALLOCATOR(BranchInstruction, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  BranchInstruction(void) = delete;

  // Instruction targeted by this branch. Assumed to be within the same
  // basic block as this instruction.
  GRANARY_INTERNAL_DEFINITION LabelInstruction * const target;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(BranchInstruction);
};

// Represents a control-flow instruction that is not local to a basic block,
// i.e. transfers control to another basic block.
//
// Note: A special case is that a non-local control-flow instruction can
//       redirect control back to the beginning of the basic block.
class ControlFlowInstruction final : public NativeInstruction {
 public:
  virtual ~ControlFlowInstruction(void);

  GRANARY_INTERNAL_DEFINITION
  ControlFlowInstruction(const arch::Instruction *instruction_,
                         BasicBlock *target_);

  // Return the target block of this CFI.
  BasicBlock *TargetBlock(void) const;

  GRANARY_DECLARE_DERIVED_CLASS_OF(Instruction, ControlFlowInstruction)
  GRANARY_DEFINE_NEW_ALLOCATOR(ControlFlowInstruction, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  friend class BlockFactory;

  ControlFlowInstruction(void) = delete;

  // Target block of this CFI.
  GRANARY_INTERNAL_DEFINITION mutable BasicBlock *target;

  GRANARY_INTERNAL_DEFINITION
  void ChangeTarget(BasicBlock *new_target) const;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ControlFlowInstruction);
};

}  // namespace granary

#endif  // GRANARY_CFG_INSTRUCTION_H_
