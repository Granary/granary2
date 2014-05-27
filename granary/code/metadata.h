/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_METADATA_H_
#define GRANARY_CODE_METADATA_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/arch/base.h"
#include "granary/code/register.h"
#include "granary/metadata.h"

namespace granary {

// Forward declarations.
class DecodedBasicBlock;

class LiveRegisterMetaData : public UnifiableMetaData<LiveRegisterMetaData> {
 public:
  LiveRegisterMetaData(void);

  // Tells us if we can unify our (uncommitted) meta-data with some existing
  // meta-data.
  UnificationStatus CanUnifyWith(const LiveRegisterMetaData *that) const;

  // Update the register meta-data given a block. Returns true if the analysis
  // resulted in any changes to the meta-data.
  bool AnalyzeBlock(DecodedBasicBlock *block);

  // Live architectural registers on entry to this basic block.
  mutable LiveRegisterTracker live_regs;
} __attribute__((packed));

class StackMetaData : public UnifiableMetaData<StackMetaData> {
 public:
  // Initialize the stack meta-data.
  inline StackMetaData(void)
      : has_stack_hint(false),
        behaves_like_callstack(false),
        is_leaf_function(false) {}

  // Tells us if we can unify our (uncommitted) meta-data with some existing
  // meta-data.
  UnificationStatus CanUnifyWith(const StackMetaData *that) const;

  // Can we depend on the stack hint being setup?
  mutable bool has_stack_hint:1;

  // Is the stack pointer being used in a way that is consistent with a
  // C-style call stack?
  mutable bool behaves_like_callstack:1;

  // Does this basic block look like it's part of a leaf function? That is,
  // have we accesses below the current stack pointer.
  mutable bool is_leaf_function:1;

} __attribute__((packed));

}  // namespace granary

#endif  // GRANARY_CODE_METADATA_H_
