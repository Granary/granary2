/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_UTIL_H_
#define GRANARY_UTIL_H_

#include "granary/cfg/basic_block.h"
#include "granary/metadata.h"

namespace granary {

// Get an instrumented basic block's meta-data.
//
// Note: This behaves specially with respect to `ReturnBasicBlock`s, which have
//       lazily created meta-data. If a `ReturnBasicBlock` has no meta-data,
//       then this function will not create meta-data on the return block.
template <typename T>
T *GetMetaData(InstrumentedBasicBlock *block) {
  return MetaDataCast<T *>(block->UnsafeMetaData());
}

// Get an instrumented basic block's meta-data.
template <typename T>
T *GetMetaDataStrict(InstrumentedBasicBlock *block) {
  return MetaDataCast<T *>(block->MetaData());
}

}  // namespace granary

#endif  // GRANARY_UTIL_H_
