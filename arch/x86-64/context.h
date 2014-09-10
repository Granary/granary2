/* Copyright 2012-2014 Peter Goodman, all rights reserved. */
/*
 * context.h
 *
 *  Created on: Sep 8, 2014
 *      Author: Peter Goodman
 */
#ifndef ARCH_X86_64_CONTEXT_H_
#define ARCH_X86_64_CONTEXT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/base.h"

namespace granary {
namespace arch {

// Contains the basic machine context information: general-purpose registers,
// flag information, etc.
class MachineContext {
 public:
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t r11;
  uint64_t r10;
  uint64_t r9;
  uint64_t r8;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rbp;
  uint64_t rbx;
  uint64_t rdx;
  uint64_t rcx;
  uint64_t rax;

 private:
  GRANARY_IF_EXTERNAL( MachineContext(void) = delete; )
  GRANARY_DISALLOW_COPY_AND_ASSIGN(MachineContext);
};

}  // namespace arch
}  // namespace granary

#endif  // ARCH_X86_64_CONTEXT_H_
