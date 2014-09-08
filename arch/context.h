/* Copyright 2012-2014 Peter Goodman, all rights reserved. */
/*
 * context.h
 *
 *  Created on: Sep 8, 2014
 *      Author: Peter Goodman
 */
#ifndef ARCH_CONTEXT_H_
#define ARCH_CONTEXT_H_

namespace granary {
namespace arch {

// Forward declaration. Should be a simple structure that contains the basic
// machine context information: general-purpose registers, flag information
// (if any), etc.
//
// This structure does not need to contain any information that Granary does
// not "clobber".
class MachineContext;

}  // namespace arch
}  // namespace granary

#endif /* ARCH_CONTEXT_H_ */
