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
class Context;
class BlockMetaData;
class IndirectEdge;

enum TargetStackValidity {
  kTargetStackValid,
  kTargetStackUnknown
};

// Instrument, compile, and index some basic blocks.
CachePC Translate(Context *context, AppPC pc,
                  TargetStackValidity stack_valid=kTargetStackUnknown);

// Instrument, compile, and index some basic blocks.
CachePC Translate(Context *context, BlockMetaData *meta);

// Instrument, compile, and index some basic blocks, where the entry block
// is targeted by an indirect control-transfer instruction.
CachePC Translate(Context *context, IndirectEdge *edge, BlockMetaData *meta);

// Instrument, compile, and index some basic blocks.
template <typename T>
static inline CachePC Translate(Context *context, T func_ptr) {
  return Translate(context, UnsafeCast<AppPC>(func_ptr), kTargetStackValid);
}

// Instrument, compile, and index some basic blocks that are the entrypoints
// to some native code.
CachePC TranslateEntryPoint(Context *context, BlockMetaData *meta,
                            EntryPointKind kind,
                            TargetStackValidity stack_valid=kTargetStackUnknown,
                            int entry_category=-1);

// Instrument, compile, and index some basic blocks that are the entrypoints
// to some native code.
CachePC TranslateEntryPoint(Context *context, AppPC target_pc,
                            EntryPointKind kind,
                            TargetStackValidity stack_valid=kTargetStackUnknown,
                            int entry_category=-1);

// Instrument, compile, and index some basic blocks.
template <typename T>
static inline
CachePC TranslateEntryPoint(Context *context, T func_ptr, EntryPointKind kind,
                            int entry_category=-1) {
  return TranslateEntryPoint(context, UnsafeCast<AppPC>(func_ptr), kind,
                             kTargetStackValid, entry_category);
}

}  // namespace granary

#endif  // GRANARY_TRANSLATE_H_
