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
  auto meta = block->UnsafeMetaData();
  return meta ? MetaDataCast<T *>(meta) : nullptr;
}

// Get a basic block's meta-data.
template <typename T>
inline static T *GetMetaData(BasicBlock *block) {
  if (auto inst_block = DynamicCast<InstrumentedBasicBlock *>(block)) {
    return GetMetaData<T>(inst_block);
  }
  return nullptr;
}

// Get an instrumented basic block's meta-data.
template <typename T>
inline static T *GetMetaDataStrict(InstrumentedBasicBlock *block) {
  return MetaDataCast<T *>(block->MetaData());
}

// For code editing purposes only. Sometimes Eclipse has trouble with all the
// `EnableIf` specializations, so this serves to satisfy its type checker.
#ifdef GRANARY_ECLIPSE

template <typename T>
T GetMetaData(const Instruction *instr);

template <typename T>
void SetMetaData(Instruction *, T);


#else

// Get an instruction's meta-data.
template <
  typename T,
  typename EnableIf<TypesAreEqual<T, uintptr_t>::RESULT>::Type=0
>
inline static uintptr_t GetMetaData(const Instruction *instr) {
  return instr->MetaData();
}

// Get an instruction's meta-data.
template <
  typename T,
  typename EnableIf<!TypesAreEqual<T, uintptr_t>::RESULT>::Type=0
>
inline static T GetMetaData(const Instruction *instr) {
  return instr->MetaData<T>();
}

// Set an instruction's meta-data.
inline static void SetMetaData(Instruction *instr, uintptr_t val) {
  instr->SetMetaData(val);
}

// Set an instruction's meta-data.
template <
  typename T,
  typename EnableIf<!TypesAreEqual<T, uintptr_t>::RESULT>::Type=0
>
inline static void SetMetaData(Instruction *instr, T val) {
  instr->SetMetaData<T>(val);
}

#endif  // GRANARY_ECLIPSE

// Clear an instruction's meta-data.
inline static void ClearMetaData(Instruction *instr) {
  instr->ClearMetaData();
}

}  // namespace granary

#endif  // GRANARY_UTIL_H_
