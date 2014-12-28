/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/watchpoints/client.h"

GRANARY_USING_NAMESPACE granary;

namespace {
enum {
  kMaxSetBit = 31,
  kTypeTableSize = 4096
};

// Uses a combination of (return address, log2 size) to identify a type.
struct Type {
  const Type *next;
  size_t size_order;
  uintptr_t ret_address;
};

typedef LinkedListIterator<const Type> ConstTypeIterator;

// Array of types for serving type allocations.
static Type gTypes[kMaxWatchpointTypeId + 1];

// Did we run out of type ids?
static bool gNoMoreTypeIds = false;

// Array of `Type` lists, with locks protecting each list.
struct TypeList {
  ReaderWriterLock types_lock;
  const Type *types;
} static gTypeTable[kTypeTableSize];

// Search the type lists for the matching type.
static const Type *FindType(TypeList *ls, uint64_t ret_address, size_t size) {
  for (auto type : ConstTypeIterator(ls->types)) {
    if (type->ret_address == ret_address && type->size_order == size) {
      return type;
    }
  }
  return nullptr;
}

// The next type Id that can be assigned.
static std::atomic<uint64_t> gNextTypeId;

static uint16_t IdOf(const Type *type) {
  return static_cast<uint16_t>(type - &(gTypes[0]));
}

// Create a new type.
static const Type *CreateType(TypeList *ls, uint64_t ret_address,
                              size_t size_order) {
  // Double check to resolve a race.
  if (auto type = FindType(ls, ret_address, size_order)) return type;

  auto type_id = gNextTypeId.fetch_add(1);
  if (kMaxWatchpointTypeId <= type_id) {
    if (!gNoMoreTypeIds) {
      gNoMoreTypeIds = true;
      os::Log(os::LogDebug, "WARNING: Ran out of type IDs.");
    }
    return nullptr;
  }
  auto new_type = &(gTypes[type_id]);
  new_type->ret_address = ret_address;
  new_type->size_order = size_order;
  new_type->next = ls->types;
  ls->types = new_type;
  return new_type;
}

}  // namespace

// Returns the type id for some `(return address, allocation size)` pair.
uint64_t TypeIdFor(uintptr_t ret_address, size_t num_bytes) {
  size_t size_order = 0;
  if (num_bytes) {
    size_order = 63UL - static_cast<size_t>(__builtin_clzl(num_bytes));
    GRANARY_ASSERT(size_order <= kMaxSetBit);
  }
  const auto type_list = &(gTypeTable[ret_address % kTypeTableSize]);
  const Type *type(nullptr);
  {
    ReadLockedRegion locker(&(type_list->types_lock));
    type = FindType(type_list, ret_address, size_order);
  }
  if (!type) {
    WriteLockedRegion locker(&(type_list->types_lock));
    if (!(type = CreateType(type_list, ret_address, size_order))) {
      return kMaxWatchpointTypeId;
    }
  }
  return IdOf(type);
}

// Apply a function to every type.
void ForEachType(std::function<void(uint64_t type_id,
                                    granary::AppPC ret_address,
                                    size_t size_order)> func) {
  const auto max_id = gNextTypeId.load(std::memory_order_relaxed);
  auto id = 0ULL;
  for (const auto &type : gTypes) {
    func(id, reinterpret_cast<AppPC>(type.ret_address), type.size_order);
    if (++id >= max_id) return;
  }
}

// Returns the approximate size (in bytes) of a given type.
size_t SizeOfType(uint64_t type_id) {
  return 1UL << gTypes[type_id].size_order;
}

GRANARY_ON_CLIENT_INIT() {
  gNoMoreTypeIds = false;
  gNextTypeId.store(0);
  memset(gTypes, 0, sizeof gTypes);
  memset(gTypeTable, 0, sizeof gTypeTable);
}
