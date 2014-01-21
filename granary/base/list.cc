/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/list.h"

namespace granary {

ListHead::ListHead(void)
    : prev(nullptr),
      next(nullptr) {}

// Remove an element from a list.
void ListHead::Unlink(void) {
  Chain(prev, next);
  prev = nullptr;
  next = nullptr;
}

// Get the object that comes after the object that contains this list head.
ListHead *ListHead::GetFirst(void) {
  ListHead *first(this);
  ListHead *next_first_(nullptr);
  ListHead *next_first(prev);
  for (; GRANARY_UNLIKELY(nullptr != next_first); ) {
    next_first_ = next_first->prev;
    first = next_first;
    next_first = next_first_;
  }
  return first;
}

// Get the object that comes after the object that contains this list head.
ListHead *ListHead::GetLast(void) {
  ListHead *last(this);
  ListHead *next_last_(nullptr);
  ListHead *next_last(prev);
  for (; GRANARY_UNLIKELY(nullptr != next_last); ) {
    next_last_ = next_last->next;
    last = next_last;
    next_last = next_last_;
  }
  return last;
}

// Compute the byte offset of the list head within its contained object.
uintptr_t ListHead::GetListOffset(const void *object) const {
  auto list_address = reinterpret_cast<uintptr_t>(this);
  auto object_address = reinterpret_cast<uintptr_t>(object);
  return list_address - object_address;
}

// Return a pointer to another object, given a pointer to the object that
// contains the current list head, and given a pointer to a list head in the
// other object.
uintptr_t ListHead::GetObject(const void *object,
                              const ListHead *other_list) const {
  if (!other_list) {
    return 0;
  }
  auto other_list_address = reinterpret_cast<uintptr_t>(other_list);
  return other_list_address - GetListOffset(object);
}

// Return a pointer to the list head contained in another object, given pointers
// to the object containing this list head and the other list head.
ListHead *ListHead::GetList(const void *this_object,
                            const void *that_object) const {
  if (!that_object) {
    return nullptr;
  }
  auto that_object_address = reinterpret_cast<uintptr_t>(that_object);
  return reinterpret_cast<ListHead *>(
      that_object_address + GetListOffset(this_object));
}

// Chain together two list heads.
void ListHead::Chain(ListHead *first, ListHead *second) {
  if (first) {
    first->next = second;
  }
  if (second) {
    second->prev = first;
  }
}

}  // namespace granary
