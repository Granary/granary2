/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_TRANSLATE_H_
#define GRANARY_TRANSLATE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/cast.h"
#include "granary/base/pc.h"

namespace granary {

// Forward declarations.
class ContextInterface;
class BlockMetaData;

// Instrument, compile, and index some basic blocks.
template <typename R, typename... Args>
static inline CachePC Translate(ContextInterface *context,
                                R (*func_ptr)(Args...)) {
  return Translate(context, UnsafeCast<AppPC>(func_ptr));
}

// Instrument, compile, and index some basic blocks.
CachePC Translate(ContextInterface *context, AppPC pc);

// Instrument, compile, and index some basic blocks.
CachePC Translate(ContextInterface *context, BlockMetaData *meta);

}  // namespace granary

#endif  // GRANARY_TRANSLATE_H_
