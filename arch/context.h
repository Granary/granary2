/* Copyright 2012-2014 Peter Goodman, all rights reserved. */
/*
 * context.h
 *
 *  Created on: Sep 8, 2014
 *      Author: Peter Goodman
 */
#ifndef ARCH_CONTEXT_H_
#define ARCH_CONTEXT_H_

#ifdef GRANARY_INTERNAL
# include "arch/base.h"

# include "granary/base/base.h"
# include "granary/base/new.h"
# include "granary/base/pc.h"
#endif

namespace granary {
namespace arch {

// Forward declaration. Should be a simple structure that contains the basic
// machine context information: general-purpose registers, flag information
// (if any), etc.
//
// This structure does not need to contain any information that Granary does
// not "clobber".
class MachineContext;

#ifdef GRANARY_INTERNAL
// Represents a machine context callback. A machine context callback is a
// client function that takes a single argument: a pointer to a
// `MachineContext`. There are two parts to this function: the function itself,
// and the "wrapper" around it which is responsible for saving/restoring the
// machine context.
//
// If the code cache is far from the location of the context callback, then
// indirection might be required to call the callback from the wrapper,
// therefore this structure acts as a sort of dual to `NativeAddress`.
//
// This structure also records the cache target of the wrapped callback.
class MachineContextCallback {
 public:
  MachineContextCallback(AppPC callback_, CachePC wrapped_callback_)
      : callback(callback_),
        wrapped_callback(wrapped_callback_) {}

  GRANARY_DEFINE_NEW_ALLOCATOR(MachineContextCallback, {
    SHARED = true,
    ALIGNMENT = 16
  })

   // Native target of the callback.
  const AppPC callback;

  // Wrapped version of the callback (located in the edge cache) that is able
  // to save/restore
  CachePC wrapped_callback;

 private:
  MachineContextCallback(void) = delete;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(MachineContextCallback);
};
#endif  // GRANARY_INTERNAL

}  // namespace arch
}  // namespace granary

#endif /* ARCH_CONTEXT_H_ */
