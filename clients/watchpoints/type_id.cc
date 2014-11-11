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
  Type *next;
  union {
    struct {
      uint64_t id:16;
      uint64_t ret_address:48;
    };
    uint64_t as_value;
  };

  GRANARY_DEFINE_NEW_ALLOCATOR(Type, {
    ALIGNMENT = 8,
    SHARED = false
  });
} __attribute__((packed));

typedef LinkedListIterator<Type> TypeIterator;

static_assert(2 * sizeof(uint64_t) == sizeof(Type),
    "Invalid structure packing for type `struct Type`.");

// Array of `Type` lists, with locks protecting each list.
struct TypeList {
  ReaderWriterLock type_lock;
  Type *type;
} sizes[MAX_SET_BIT + 1];

// Search the type lists for the matching type.
static Type *FindType(TypeList *ls, uint64_t value_mask) {
  ReadLockedRegion locker(&(ls->type_lock));
  for (auto type : TypeIterator(ls->type)) {
    if (!(value_mask & type->as_value)) {
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

// Create a new type.
static Type *CreateType(TypeList *ls, uint64_t value_mask) {
  auto type = new Type;
  type->ret_address = value_mask;
  WriteLockedRegion locker(&(ls->type_lock));

  // Simplistic but incomplete check to detect races.
  if (!ls->type || 0 != (value_mask & ls->type->as_value)) {
    type->id = AllocateTypeId();
    type->next = ls->type;
    ls->type = type;
  } else {
    delete type;  // Found (at least one) race.
  }
  return ls->type;
}

}  // namespace

// Returns the type id for some `(return address, allocation size)` pair.
uint64_t TypeIdFor(uintptr_t ret_address, size_t num_bytes) {
  const auto value_mask = ret_address & 0xFFFFFFFFFFFFULL;
  const auto type_list = &(sizes[63 - __builtin_clzl(num_bytes)]);
  auto type = FindType(type_list, value_mask);
  if (!type) {
    type = CreateType(type_list, value_mask);
  }
  return type->id;
}

GRANARY_ON_CLIENT_INIT() {
  memset(sizes, 0, sizeof sizes);
}
