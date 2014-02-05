/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/list.h"
#include "granary/base/new.h"
#include "granary/base/options.h"
#include "granary/breakpoint.h"
#include "granary/metadata.h"

namespace granary {

// Initialize Granary's internal translation meta-data.
TranslationMetaData::TranslationMetaData(void)
    : source(),
      native_pc(nullptr) {}

// Hash the translation meta-data.
void TranslationMetaData::Hash(HashFunction *hasher) const {
  hasher->Accumulate(this);
}

// Compare two translation meta-data objects for equality.
bool TranslationMetaData::Equals(const TranslationMetaData *meta) const {
  return source == meta->source && native_pc == meta->native_pc;
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

inline static LinkedListIterator<detail::meta::MetaDataInfo>
MetaDataInfos(void) {
  return LinkedListIterator<detail::meta::MetaDataInfo>(META);
}

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

  auto next_ptr = &META;
  for (auto curr(META); curr; ) {  // Find the insertion point.
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

// Get some specific meta-data from some generic meta-data.
void *GetMetaData(const MetaDataInfo *info, GenericMetaData *meta) {
  GRANARY_IF_DEBUG( granary_break_on_fault_if(!info->is_registered); )
  auto meta_ptr = reinterpret_cast<uintptr_t>(meta);
  return reinterpret_cast<void *>(meta_ptr + info->offset);
}

}  // namespace meta
}  // namespace detail

// Initialize a new meta-data instance. This involves separately initializing
// the contained meta-data within this generic meta-data.
GenericMetaData::GenericMetaData(AppProgramCounter pc) {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  for (auto meta : MetaDataInfos()) {
    meta->initialize(reinterpret_cast<void *>(this_ptr + meta->offset));
  }

  // Default-initialize the translation meta-data.
  auto trans = MetaDataCast<TranslationMetaData *>(this);
  if (pc) {
    auto module = FindModuleByPC(pc);
    trans->source = module->OffsetOf(pc);
  }
  trans->native_pc = pc;
}

// Destroy a meta-data instance. This involves separately destroying the
// contained meta-data within this generic meta-data.
GenericMetaData::~GenericMetaData(void) {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  for (auto meta : MetaDataInfos()) {
    meta->destroy(reinterpret_cast<void *>(this_ptr + meta->offset));
  }
}

// Create a copy of some meta-data and return a new instance of the copied
// meta-data.
GenericMetaData *GenericMetaData::Copy(void) const {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  auto that_ptr = reinterpret_cast<uintptr_t>(
      GenericMetaData::operator new(0));

  for (auto meta : MetaDataInfos()) {
    meta->copy_initialize(
        reinterpret_cast<void *>(that_ptr + meta->offset),
        reinterpret_cast<const void *>(this_ptr + meta->offset));
  }

  return reinterpret_cast<GenericMetaData *>(that_ptr);
}

// Hash all serializable meta-data contained within this generic meta-data.
void GenericMetaData::Hash(HashFunction *hasher) const {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  for (auto meta : MetaDataInfos()) {
    meta->hash(hasher, reinterpret_cast<const void *>(this_ptr + meta->offset));
  }
}

// Compare the serializable components of two generic meta-data instances for
  // strict equality.
bool GenericMetaData::Equals(const GenericMetaData *that) const {
  auto this_ptr = reinterpret_cast<uintptr_t>(this);
  auto that_ptr = reinterpret_cast<uintptr_t>(that);
  for (auto meta : MetaDataInfos()) {
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
alignas(internal::SlabAllocator) struct {
  uint8_t data[sizeof(internal::SlabAllocator)];
} static META_ALLOCATOR_MEM;

// Late-initialize the meta-data allocator.
static void InitMetaDataAllocator(void) {
  auto offset = GRANARY_ALIGN_TO(sizeof(internal::SlabList), META_SIZE);
  auto remaining_size = GRANARY_ARCH_PAGE_FRAME_SIZE - offset;
  auto max_num_allocs = remaining_size / META_SIZE;
  new (&META_ALLOCATOR_MEM) internal::SlabAllocator(
      max_num_allocs, offset, META_SIZE, META_SIZE);
}

// The actual allocator (as backed by the `META_ALLOCATOR_MEM` storage).
static internal::SlabAllocator * const META_ALLOCATOR = \
    reinterpret_cast<internal::SlabAllocator *>(&META_ALLOCATOR_MEM);

}  // namespace

// Dynamically allocate meta-data.
void *GenericMetaData::operator new(std::size_t) {
  void *address(META_ALLOCATOR->Allocate());
  VALGRIND_MALLOCLIKE_BLOCK(address, META_SIZE, 0, 0);
  return address;
}

// Dynamically free meta-data.
void GenericMetaData::operator delete(void *address) {
  META_ALLOCATOR->Free(address);
  VALGRIND_FREELIKE_BLOCK(address, META_SIZE);
}

// Initialize all meta-data. This finalizes the meta-data structures, which
// determines the runtime layout of the packed meta-data structure.
void InitMetaData(void) {
  RegisterMetaData<TranslationMetaData>();
  CAN_REGISTER_META = false;

  for (auto meta : MetaDataInfos()) {
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
