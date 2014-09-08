/* Copyright 2012-2014 Peter Goodman, all rights reserved. */
/*
 * abi.h
 *
 *  Created on: Sep 8, 2014
 *      Author: Peter Goodman
 */
#ifndef OS_ABI_H_
#define OS_ABI_H_

#include "arch/context.h"

namespace granary {
namespace os {
namespace abi {

// Returns a pointer to the Nth function call argument, given a machine context.
//
// Note: `n == 0` is the first argument.
uint64_t *NthFunctionCallArgument(arch::MachineContext *context, int n);

// Returns a pointer to the return value of a function call.
uint64_t *FunctionCallReturnValue(arch::MachineContext *context);

// Returns a pointer to the system call number for this system call, given a
// machine context.
uint64_t *SystemCallNumber(arch::MachineContext *context);

// Returns a pointer to the Nth system call argument, given a machine context.
//
// Note: `n == 0` is the first argument.
uint64_t *NthSystemCallArgument(arch::MachineContext *context, int n);

// Returns a pointer to the return value of a system call.
uint64_t *SystemCallReturnValue(arch::MachineContext *context);

}  // namespace abi
}  // namespace os
}  // namespace granary

#endif  // OS_ABI_H_
