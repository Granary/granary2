/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_INLINE_ASSEMBLY_H_
#define GRANARY_CODE_INLINE_ASSEMBLY_H_

#include "granary/base/base.h"  // For `size_t`.

#ifdef GRANARY_INTERNAL
# include "granary/base/container.h"
# include "granary/base/new.h"
# include "granary/base/refcount.h"

# include "granary/cfg/operand.h"
#endif

namespace granary {

enum {
  MAX_NUM_INLINE_ASM_SCOPES = 4,
  MAX_NUM_INLINE_VARS = 8
};

#ifdef GRANARY_INTERNAL
// Forward declarations.
class LocalControlFlowGraph;
class DecodedBasicBlock;
class LabelInstruction;
class Instruction;

// A variable in the inline assembly. Variables are untyped, and assumed to
// be used in the correct way from the inline assembly instructions themselves.
union InlineAssemblyVariable {
 public:
  InlineAssemblyVariable(void) = default;

  // Initialize the inline assembly variable with a particular operand.
  explicit InlineAssemblyVariable(Operand *op);

  Container<RegisterOperand> reg;
  Container<MemoryOperand> mem;
  Container<ImmediateOperand> imm;

  // This variable is actually a label. Labels can be referenced before they
  // are placed in the instruction stream, i.e. before they are defined.
  LabelInstruction *label;
};

static_assert(0 == offsetof(InlineAssemblyVariable, reg),
    "Invalid structure packing of `union InlineAssemblyVariable`.");

static_assert(0 == offsetof(InlineAssemblyVariable, mem),
    "Invalid structure packing of `union InlineAssemblyVariable`.");

static_assert(0 == offsetof(InlineAssemblyVariable, imm),
    "Invalid structure packing of `union InlineAssemblyVariable`.");


// Represents a scope of inline assembly. Within this scope, several virtual
// registers are live.
class InlineAssemblyScope : public UnownedCountedObject {
 public:
  // Initialize the input variables to the scope.
  explicit InlineAssemblyScope(std::initializer_list<Operand *> inputs);
  virtual ~InlineAssemblyScope(void);

  GRANARY_DEFINE_NEW_ALLOCATOR(InlineAssemblyScope, {
    SHARED = true,
    ALIGNMENT = 1
  })

  InlineAssemblyScope(void) = delete;

  // Variables used/referenced/created within the scope.
  InlineAssemblyVariable vars[MAX_NUM_INLINE_VARS];
  BitSet<MAX_NUM_INLINE_VARS> var_is_initialized;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(InlineAssemblyScope);
};

// Represents a block of inline assembly instructions.
class InlineAssemblyBlock {
 public:
  // Initialize this block of inline assembly.
  //
  // Note: This will acquire a reference count on the scope referenced by this
  //       block.
  InlineAssemblyBlock(InlineAssemblyScope *scope_, const char *assembly_);

  // Destroy this block of inline assembly.
  //
  // Note: This will delete the associated scope iff, after releasing a
  //       reference to the scope, the scope has no more references pointing
  //       to it.
  ~InlineAssemblyBlock(void);

  // Compile this inline assembly into some instructions within the block
  // `block`. This places the inlined instructions before `instr`, which is
  // assumed to be the `AnnotationInstruction` containing the inline assembly
  // instructions.
  //
  // Note: This has an architecture-specific implementation.
  void Compile(LocalControlFlowGraph *cfg, DecodedBasicBlock *block,
               Instruction *instr);

  GRANARY_DEFINE_NEW_ALLOCATOR(InlineAssemblyScope, {
    SHARED = true,
    ALIGNMENT = 1
  })

 private:
  InlineAssemblyScope * const scope;
  const char * const assembly;
};
#endif  // GRANARY_INTERNAL

#define GRANARY_DEFINE_ASM_OP(arch, param, ret) \
  inline const char *operator"" _ ## arch (const char *param, size_t) { \
    return ret; \
  }

GRANARY_DEFINE_ASM_OP(x86, , nullptr)  // 32-bit x86.
GRANARY_DEFINE_ASM_OP(x86_64, lines, lines)  // 64-bit x86.
GRANARY_DEFINE_ASM_OP(arm, , nullptr)
GRANARY_DEFINE_ASM_OP(armv7, , nullptr)
GRANARY_DEFINE_ASM_OP(thumb, , nullptr)
GRANARY_DEFINE_ASM_OP(mips, , nullptr)
GRANARY_DEFINE_ASM_OP(sparc, , nullptr)
GRANARY_DEFINE_ASM_OP(ppc, , nullptr)
}  // namespace granary

// Fake user defined literals, needed until this Eclipse syntax highlighting
// bug (`https://bugs.eclipse.org/bugs/show_bug.cgi?id=379684`) is closed.
#ifdef GRANARY_ECLIPSE
# define _x86
# define _x86_64
# define _arm
# define _armv7
# define _thumb
# define _mips
# define _sparc
# define _ppc
#else
# define IF_ECLIPSE__x86
# define IF_ECLIPSE__x86_64
# define IF_ECLIPSE__arm
# define IF_ECLIPSE__armv7
# define IF_ECLIPSE__thumb
# define IF_ECLIPSE__mips
# define IF_ECLIPSE__sparc
# define IF_ECLIPSE__ppc
#endif  // GRANARY_ECLIPSE

#undef GRANARY_DEFINE_ASM_OP

#endif  // GRANARY_CODE_INLINE_ASSEMBLY_H_
