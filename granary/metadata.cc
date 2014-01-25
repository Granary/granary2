/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#include "granary/metadata.h"

namespace granary {
namespace detail {
namespace meta {

// Assume that stateful meta-data is equivalent, which can be expressed as
// not contributing any new information to the hasher.
void FakeHash(HashFunction *, const void *) { }

// Assume all stateful meta-data is equivalent.
bool FakeCompareEquals(const void *, const void *) {
  return true;
}

}  // namespace meta
}  // namespace detail

GenericMetaData *GenericMetaData::Copy(void) const {
  return nullptr;  // TODO(pag): Implement this.
}

void GenericMetaData::Hash(HashFunction *hasher) const {
  GRANARY_UNUSED(hasher);  // TODO(pag): Implement this.
}

bool GenericMetaData::Equals(const GenericMetaData *meta) const {
  GRANARY_UNUSED(meta);
  return false;
}

GenericMetaData *CopyOrCreate(const GenericMetaData *meta) {
  if (meta) {
    return meta->Copy();
  }
  return new GenericMetaData;
}

}  // namespace granary
