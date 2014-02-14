/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_LIST_H_
#define GRANARY_BASE_LIST_H_

#include "granary/base/base.h"

#ifdef GRANARY_DEBUG
# include "granary/breakpoint.h"
#endif

namespace granary {

// Generic, type-safe, embeddable list implementation, similar to in the
// Linux kernel.
class ListHead {
 public:
  ListHead(void);

#ifdef GRANARY_DEBUG
  // Ensure that the correct `object` pointer is being passed to the `ListHead`
  // public APIs.
  template <typename T>
  void CheckObject(const T *object) const {
    const void *obj_ptr(object);
    const void *next_obj_ptr(object + 1);
    const void *this_ptr(this);
    const void *next_this_ptr(this + 1);
    granary_break_on_fault_if(!(obj_ptr <= this_ptr && next_this_ptr <= next_obj_ptr));
  }
#endif

  // Returns true if this list element is attached to any other list elements.
  inline bool IsAttached(void) const {
    return nullptr != prev || nullptr != next;
  }

  // Get the object that comes after the object that contains this list head.
  template <typename T>
  T *GetNext(const T *object) const {
    GRANARY_IF_DEBUG( CheckObject(object); )
    if (!next) {
      return nullptr;
    }

    return reinterpret_cast<T *>(GetObject(object, next));
  }

  // Set the object that should go after this object's list head.
  template <typename T>
  void SetNext(const T *this_object, const T *that_object) {
    GRANARY_IF_DEBUG( CheckObject(this_object); )
    if (!that_object) {
      return;
    }

    ListHead *that_list(GetList(this_object, that_object));
    Chain(that_list->GetLast(), next);
    Chain(this, that_list->GetFirst());
  }

  // Get the object that comes before the object that contains this list head.
  template <typename T>
  T *GetPrevious(const T *object) const {
    GRANARY_IF_DEBUG( CheckObject(object); )
    if (!prev) {
      return nullptr;
    }

    return reinterpret_cast<T *>(GetObject(object, prev));
  }

  // Set the object that should go before this object's list head.
  template <typename T>
  void SetPrevious(const T *this_object, const T *that_object) {
    GRANARY_IF_DEBUG( CheckObject(this_object); )
    if (!that_object) {
      return;
    }

    ListHead *that_list(GetList(this_object, that_object));
    Chain(prev, that_list->GetFirst());
    Chain(that_list->GetLast(), this);
  }

  // Unlink this list head from the list.
  void Unlink(void);

 private:
  ListHead *GetFirst(void);
  ListHead *GetLast(void);
  uintptr_t GetListOffset(const void *object) const;
  uintptr_t GetObject(const void *object, const ListHead *other_list) const;
  ListHead *GetList(const void *this_object, const void *that_object) const;
  static void Chain(ListHead *first, ListHead *second);

  ListHead *prev;
  ListHead *next;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ListHead);
};

// Generic iterator for simple linked lists with public `next` fields.
template <typename T>
class LinkedListIterator {
 public:
  typedef LinkedListIterator<T> Iterator;

  LinkedListIterator(void)
      : curr(nullptr) {}

  LinkedListIterator(const Iterator &that)  // NOLINT
      : curr(that.curr) {}

  LinkedListIterator(const Iterator &&that)  // NOLINT
      : curr(that.curr) {}

  explicit LinkedListIterator(T *first)
      : curr(first) {}

  inline Iterator begin(void) const {
    return *this;
  }

  inline Iterator end(void) const {
    return Iterator(nullptr);
  }

  inline void operator++(void) {
    curr = curr->next;
  }

  inline bool operator!=(const Iterator &that) const {
    return curr != that.curr;
  }

  inline T *operator*(void) const {
    return curr;
  }

  // Returns the last valid element from an iterator.
  static T *Last(LinkedListIterator<T> elems) {
    T *last(nullptr);
    for (auto elem : elems) {
      last = elem;
    }
    return last;
  }

  // Returns the last valid element from an iterator.
  static inline T *Last(T *elems_ptr) {
    return Last(LinkedListIterator<T>(elems_ptr));
  }

 private:
  T *curr;
};

}  // namespace granary

#endif  // GRANARY_BASE_LIST_H_
