/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "granary/base/new.h"

#include "granary/cfg/instruction.h"

#include "granary/code/fragment.h"
#include "granary/code/ssa.h"

#include "granary/util.h"

#define GRANARY_DEFINE_SSA_ALLOCATOR(cls) \
  void *cls::operator new(std::size_t) { \
    return new SSANodeMemory; \
  } \
  void *cls::operator new(std::size_t, void *mem) { \
    return mem; \
  } \
  void cls::operator delete(void *address) { \
    delete reinterpret_cast<SSANodeMemory *>(address); \
  }

namespace granary {

SSAOperand::SSAOperand(void)
    : action(kSSAOperandActionInvalid),
      operand(nullptr),
      reg_web() {}

SSAInstruction::SSAInstruction(void)
    : num_ops(0) {}

}  // namespace granary
