/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#include "granary/metadata.h"

namespace granary {
namespace {

// Global list of registered meta-data descriptors.
static detail::meta::MetaDataInfo *META = nullptr;

// The total size of the meta-data.
static size_t META_SIZE = 0;

// The total alignment of the meta-data.
static size_t META_ALIGN = 0;

}  // namespace
namespace detail {
namespace meta {

// Assume that stateful meta-data is equivalent, which can be expressed as
// not contributing any new information to the hasher.
void FakeHash(HashFunction *, const void *) { }

// Assume all stateful meta-data is equivalent.
bool FakeCompareEquals(const void *, const void *) {
  return true;
}

// Register some meta-data with Granary. This arranges for all meta-data to be
// in decreasing order of `(size, align)`. That way Granary can pack the meta-
// data together nicely into one large super structure.
void RegisterMetaData(const MetaDataInfo *meta_) {
  auto meta = const_cast<MetaDataInfo *>(meta_);
  auto next_ptr = &META;
  for (auto curr(META); curr; ) {
    if ((meta->size > curr->size) ||
        (meta->size == curr->size && meta->align > curr->align)) {
      break;  // Found the insertion point.
    }
    next_ptr = &(curr->next);
    curr = curr->next;
  }

  // Chain the meta-data into the list.
  meta->next = *next_ptr;
  *next_ptr = meta;
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

// Initialize all meta-data. This finalizes the meta-data structures, which
// determines the runtime layout of the packed meta-data structure.
void InitMetaData(void) {
  auto meta = META;

  META_ALIGN = meta->align;
  for (; meta; meta = meta->next) {
    if (META_SIZE) {
      META_SIZE += GRANARY_ALIGN_FACTOR(META_SIZE, meta->align);
    }
    meta->offset = static_cast<int>(META_SIZE);
    META_SIZE += meta->size;
  }
}

}  // namespace granary
