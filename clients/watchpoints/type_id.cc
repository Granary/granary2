/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/watchpoints/client.h"

GRANARY_USING_NAMESPACE granary;

namespace {
enum {
  kMaxSetBit = 31
};

// Uses a combination of (return address, log2 size) to identify a type.
struct Type {
  const Type *next;
  size_t size_order:16;
  uintptr_t ret_address:48;
} __attribute__((packed));

typedef LinkedListIterator<const Type> ConstTypeIterator;

static_assert(2 * sizeof(uint64_t) == sizeof(Type),
    "Invalid structure packing for type `struct Type`.");

// Array of types for serving type allocations.
Type types[kMaxWatchpointTypeId + 1];

// Array of `Type` lists, with locks protecting each list.
struct TypeList {
  ReaderWriterLock types_lock;
  const Type *types;
} sizes[kMaxSetBit + 1];

// Search the type lists for the matching type.
static const Type *FindType(TypeList *ls, uint64_t ret_address_) {
  auto ret_address = ret_address_ & 0xFFFFFFFFFFFFULL;
  for (auto type : ConstTypeIterator(ls->types)) {
    if (type->ret_address == ret_address) {
      return type;
    }
  }
  return nullptr;
}

// The next type Id that can be assigned.
static std::atomic<uint64_t> gNextTypeId = ATOMIC_VAR_INIT(0);

// Allocate a new type id.
static uint64_t AllocateTypeId(void) {
  auto id = gNextTypeId.fetch_add(1);
  GRANARY_ASSERT(kMaxWatchpointTypeId >= id);
  return id;
}

static uint16_t IdOf(const Type *type) {
  return static_cast<uint16_t>(type - &(types[0]));
}

// Create a new type.
static const Type *CreateType(TypeList *ls, uint64_t ret_address,
                              size_t size_order) {
  if (auto type = FindType(ls, ret_address)) {
    return type;  // Double check to resolve a race.
  }
  auto new_type = &(types[AllocateTypeId()]);
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
  const auto type_list = &(sizes[size_order]);
  const Type *type(nullptr);
  {
    ReadLockedRegion locker(&(type_list->types_lock));
    type = FindType(type_list, ret_address);
  }
  if (!type) {
    WriteLockedRegion locker(&(type_list->types_lock));
    type = CreateType(type_list, ret_address, size_order);
  }
  return IdOf(type);
}

// Apply a function to every type.
void ForEachType(std::function<void(uint64_t type_id,
                                    granary::AppPC ret_address,
                                    size_t size_order)> func) {
  const auto max_id = gNextTypeId.load(std::memory_order_relaxed);
  auto id = 0ULL;
  for (const auto &type : types) {
    func(id, reinterpret_cast<AppPC>(type.ret_address), type.size_order);
    if (++id >= max_id) return;
  }
}

GRANARY_ON_CLIENT_INIT() {
  memset(types, 0, sizeof types);
  memset(sizes, 0, sizeof sizes);
}
