/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/new.h"
#include "granary/breakpoint.h"
#include "granary/metadata.h"

namespace granary {

// Hash the translation meta-data.
void TranslatioMetaData::Hash(HashFunction *hasher) const {
  hasher->Accumulate(raw_bits);
}

// Compare two translation meta-data objects for equality.
bool TranslatioMetaData::Equals(const TranslatioMetaData *meta) const {
  return raw_bits == meta->raw_bits;
}

namespace {

// Global list of registered meta-data descriptors.
static detail::meta::MetaDataInfo *META = nullptr;

// The total size of the meta-data.
static size_t META_SIZE = 0;

// The total alignment of the meta-data.
static size_t META_ALIGN = 0;

// Is it too late to register meta-data?
static bool CAN_REGISTER_META = true;

}  // namespace
namespace detail {
namespace meta {

// Register some meta-data with Granary. This arranges for all meta-data to be
// in decreasing order of `(size, align)`. That way Granary can pack the meta-
// data together nicely into one large super structure.
void RegisterMetaData(const MetaDataInfo *meta_) {
  granary_break_on_fault_if(!CAN_REGISTER_META);

  auto meta = const_cast<MetaDataInfo *>(meta_);
  if (meta->is_registered) {
    return;
  }

  // Find the insertion point.
  auto next_ptr = &META;
  for (auto curr(META); curr; ) {
    if ((meta->size > curr->size) ||
        (meta->size == curr->size && meta->align > curr->align)) {
      break;
    }
    next_ptr = &(curr->next);
    curr = curr->next;
  }

  // Chain the meta-data into the list.
  meta->is_registered = true;
  meta->next = *next_ptr;
  *next_ptr = meta;
}

}  // namespace meta
}  // namespace detail

// Initialize a new meta-data instance. This involves separately initializing
// the contained meta-data within this generic meta-data.
GenericMetaData::GenericMetaData(void) {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  for (auto meta = META; meta; meta = meta->next) {
    meta->initialize(reinterpret_cast<void *>(this_ptr + meta->offset));
  }
}

// Destroy a meta-data instance. This involves separately destroying the
// contained meta-data within this generic meta-data.
GenericMetaData::~GenericMetaData(void) {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  for (auto meta = META; meta; meta = meta->next) {
    meta->destroy(reinterpret_cast<void *>(this_ptr + meta->offset));
  }
}

// Create a copy of some meta-data and return a new instance of the copied
// meta-data.
GenericMetaData *GenericMetaData::Copy(void) const {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  auto that_ptr = reinterpret_cast<uintptr_t>(
      GenericMetaData::operator new(0));

  for (auto meta = META; meta; meta = meta->next) {
    meta->copy_initialize(
        reinterpret_cast<void *>(that_ptr + meta->offset),
        reinterpret_cast<const void *>(this_ptr + meta->offset));
  }

  return reinterpret_cast<GenericMetaData *>(that_ptr);
}

// Hash all serializable meta-data contained within this generic meta-data.
void GenericMetaData::Hash(HashFunction *hasher) const {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  for (auto meta = META; meta; meta = meta->next) {
    meta->hash(hasher, reinterpret_cast<const void *>(this_ptr + meta->offset));
  }
}

// Compare the serializable components of two generic meta-data instances for
  // strict equality.
bool GenericMetaData::Equals(const GenericMetaData *that) const {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  auto that_ptr = reinterpret_cast<uintptr_t>(that);
  for (auto meta = META; meta; meta = meta->next) {
    if (!meta->is_serializable) {
      continue;
    }
    auto this_meta = reinterpret_cast<const void *>(this_ptr + meta->offset);
    auto that_meta = reinterpret_cast<const void *>(that_ptr + meta->offset);
    if (!meta->compare_equals(this_meta, that_meta)) {
      return false;
    }
  }
  return true;
}

namespace {

// Storage space to hold the meta-data allocator.
alignas(detail::SlabAllocator) struct {
  uint8_t data[sizeof(detail::SlabAllocator)];
} META_ALLOCATOR_MEM;

// Late-initialize the meta-data allocator.
static void InitMetaDataAllocator(void) {
  auto offset = GRANARY_ALIGN_TO(sizeof(detail::SlabList), META_SIZE);
  auto remaining_size = GRANARY_ARCH_PAGE_FRAME_SIZE - offset;
  auto max_num_allocs = remaining_size / META_SIZE;
  new (&META_ALLOCATOR_MEM) detail::SlabAllocator(
      max_num_allocs, offset, META_SIZE, META_SIZE);
}

// The actual allocator (as backed by the `META_ALLOCATOR_MEM` storage).
static detail::SlabAllocator &META_ALLOCATOR = \
    *UnsafeCast<detail::SlabAllocator *>(&META_ALLOCATOR_MEM);

}  // namespace

// Dynamically allocate meta-data.
void *GenericMetaData::operator new(std::size_t) {
  void *address(META_ALLOCATOR.Allocate());
  VALGRIND_MALLOCLIKE_BLOCK(address, META_SIZE, 0, 0);
  return address;
}

// Dynamically free meta-data.
void GenericMetaData::operator delete(void *address) {
  META_ALLOCATOR.Free(address);
  VALGRIND_FREELIKE_BLOCK(address, META_SIZE);
}

// Initialize all meta-data. This finalizes the meta-data structures, which
// determines the runtime layout of the packed meta-data structure.
void InitMetaData(void) {
  RegisterMetaData<TranslatioMetaData>();
  CAN_REGISTER_META = false;

  for (auto meta = META; meta; meta = meta->next) {
    if (META_SIZE) {
      META_SIZE += GRANARY_ALIGN_FACTOR(META_SIZE, meta->align);
    } else {
      META_ALIGN = meta->align;
    }
    meta->offset = META_SIZE;
    META_SIZE += meta->size;
  }

  META_SIZE += GRANARY_ALIGN_FACTOR(META_SIZE, META_ALIGN);
  InitMetaDataAllocator();
}

}  // namespace granary
