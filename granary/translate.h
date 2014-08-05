/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_TRANSLATE_H_
#define GRANARY_TRANSLATE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/cast.h"
#include "granary/base/pc.h"

#include "granary/entry.h"

namespace granary {

// Forward declarations.
class ContextInterface;
class BlockMetaData;
class IndirectEdge;

enum TargetStackValidity {
  TRANSLATE_STACK_VALID,
  TRANSLATE_STACK_UNKNOWN
};

// Instrument, compile, and index some basic blocks.
CachePC Translate(ContextInterface *context, AppPC pc,
                  TargetStackValidity stack_valid=TRANSLATE_STACK_UNKNOWN);

// Instrument, compile, and index some basic blocks.
CachePC Translate(ContextInterface *context, BlockMetaData *meta);

// Instrument, compile, and index some basic blocks, where the entry block
// is targeted by an indirect control-transfer instruction.
CachePC Translate(ContextInterface *context, IndirectEdge *edge,
                  AppPC target_pc);

// Instrument, compile, and index some basic blocks.
template <typename T>
static inline CachePC Translate(ContextInterface *context, T func_ptr) {
  return Translate(context, UnsafeCast<AppPC>(func_ptr), TRANSLATE_STACK_VALID);
}

// Instrument, compile, and index some basic blocks that are the entrypoints
// to some native code.
CachePC TranslateEntryPoint(ContextInterface *context, BlockMetaData *meta,
                            EntryPointKind kind, int entry_category=-1);

}  // namespace granary

#endif  // GRANARY_TRANSLATE_H_
