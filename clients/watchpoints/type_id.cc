/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/watchpoints/type_id.h"

GRANARY_USING_NAMESPACE granary;

namespace {

enum {
  MAX_SET_BIT = 31,
  MAX_TYPE_ID = std::numeric_limits<uint16_t>::max() >> 1U
};

// Uses a combination of (return address, log2 size) to identify a type.
struct Type {
  const Type *next;
  uintptr_t ret_address;
} __attribute__((packed));

typedef LinkedListIterator<const Type> ConstTypeIterator;

static_assert(2 * sizeof(uint64_t) == sizeof(Type),
    "Invalid structure packing for type `struct Type`.");

// Array of types for serving type allocations.
Type types[MAX_TYPE_ID + 1];

// Array of `Type` lists, with locks protecting each list.
struct TypeList {
  ReaderWriterLock type_lock;
  const Type *type;
} sizes[MAX_SET_BIT + 1];

// Search the type lists for the matching type.
static const Type *FindType(TypeList *ls, uint64_t ret_address) {
  for (auto type : ConstTypeIterator(ls->type)) {
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
  GRANARY_ASSERT(MAX_TYPE_ID >= id);
  return id;
}

static uint16_t IdOf(const Type *type) {
  return static_cast<uint16_t>(type - &(types[0]));
}

// Create a new type.
static const Type *CreateType(TypeList *ls, uint64_t ret_address) {
  if (auto type = FindType(ls, ret_address)) {
    return type;  // Double check to resolve a race.
  }
  auto new_type = &(types[AllocateTypeId()]);
  new_type->ret_address = ret_address;
  new_type->next = ls->type;
  ls->type = new_type;
  return new_type;
}

}  // namespace

// Returns the type id for some `(return address, allocation size)` pair.
uint64_t TypeIdFor(uintptr_t ret_address, size_t num_bytes) {
  const auto type_list = &(sizes[63 - __builtin_clzl(num_bytes)]);
  const Type *type(nullptr);
  {
    ReadLockedRegion locker(&(type_list->type_lock));
    type = FindType(type_list, ret_address);
  }
  if (!type) {
    WriteLockedRegion locker(&(type_list->type_lock));
    type = CreateType(type_list, ret_address);
  }
  return IdOf(type);
}

GRANARY_ON_CLIENT_INIT() {
  memset(types, 0, sizeof types);
  memset(sizes, 0, sizeof sizes);
}
