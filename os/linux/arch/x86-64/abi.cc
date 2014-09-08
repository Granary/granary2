/* Copyright 2012-2014 Peter Goodman, all rights reserved. */
/*
 * abi.cc
 *
 *  Created on: Sep 8, 2014
 *      Author: Peter Goodman
 */

#define GRANARY_INTERNAL
#define GRANARY_ARCH_INTERNAL

#include "os/abi.h"

#include "arch/x86-64/context.h"

namespace granary {
namespace os {
namespace abi {

// Returns a pointer to the Nth function call argument, given a machine context.
//
// Note: `n == 0` is the first argument.
uint64_t *NthFunctionCallArgument(arch::MachineContext *context, int n) {
  switch (n) {
    case 0: return &(context->rdi);
    case 1: return &(context->rsi);
    case 2: return &(context->rdx);
    case 3: return &(context->rcx);
    case 4: return &(context->r8);
    case 5: return &(context->r9);
    default: return nullptr;
  }
}

// Returns a pointer to the return value of a function call.
uint64_t *FunctionCallReturnValue(arch::MachineContext *context) {
  return &(context->rax);
}

// Returns a pointer to the system call number for this system call, given a
// machine context.
uint64_t *SystemCallNumber(arch::MachineContext *context) {
  return &(context->rax);
}

// Returns a pointer to the Nth system call argument, given a machine context.
//
// Note: `n == 0` is the first argument.
uint64_t *NthSystemCallArgument(arch::MachineContext *context, int n) {
  switch (n) {
    case 0: return &(context->rdi);
    case 1: return &(context->rsi);
    case 2: return &(context->rdx);
    case 3: return &(context->r10);
    case 4: return &(context->r8);
    case 5: return &(context->r9);
    default: return nullptr;
  }
}

// Returns a pointer to the return value of a system call.
uint64_t *SystemCallReturnValue(arch::MachineContext *context) {
  return &(context->rax);
}

}  // namespace abi
}  // namespace os
}  // namespace granary
