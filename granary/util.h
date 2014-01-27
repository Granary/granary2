/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_UTIL_H_
#define GRANARY_UTIL_H_

#include "granary/cfg/basic_block.h"
#include "granary/metadata.h"

namespace granary {

template <typename T>
T *GetMetaData(InstrumentedBasicBlock *block) {
  return MetaDataCast<T *>(block->MetaData());
}

}  // namespace granary

#endif  // GRANARY_UTIL_H_
