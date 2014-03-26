/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_UTIL_H_
#define GRANARY_UTIL_H_

#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/metadata.h"

namespace granary {

// Get an instrumented basic block's meta-data.
//
// Note: This behaves specially with respect to `ReturnBasicBlock`s, which have
//       lazily created meta-data. If a `ReturnBasicBlock` has no meta-data,
//       then this function will not create meta-data on the return block.
template <typename T>
inline static T *GetMetaData(InstrumentedBasicBlock *block) {
  return MetaDataCast<T *>(block->UnsafeMetaData());
}

// Get an instrumented basic block's meta-data.
template <typename T>
inline static T *GetMetaDataStrict(InstrumentedBasicBlock *block) {
  return MetaDataCast<T *>(block->MetaData());
}

// Get an instruction's meta-data.
template <typename T>
inline static T GetMetaData(Instruction *instr) {
  return instr->MetaData<T>();
}

// Get an instruction's meta-data.
inline static void ClearMetaData(Instruction *instr) {
  instr->ClearMetaData();
}

// Set an instruction's meta-data.
template <typename T>
void SetMetaData(Instruction *instr, T val) {
  instr->SetMetaData<T>(val);
}

}  // namespace granary

#endif  // GRANARY_UTIL_H_
