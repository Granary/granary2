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

// Forward declaration.
template <typename T> class LinkedListZipper;

namespace detail {

// Zipper element for a linked list. Allows insertion before/after the current
// element, as well as removal of the current element.
template <typename T>
class LinkedListZipperElement {
 public:
  LinkedListZipperElement(T **curr_ptr_, T *curr_)
      : curr_ptr(curr_ptr_),
        curr(curr_) {}

  inline T &operator*(void) {
    return *curr;
  }

  inline T *operator->(void) {
    return curr;
  }

  inline void InsertBefore(T *prev) {
    prev->next = curr;
    *curr_ptr = prev;
    curr_ptr = &(prev->next);
  }

  inline void InsertAfter(T *next) {
    curr->next = next;
  }

  // Unlink the current element. This will return the unlinked list element and
  // invalidate this zipper element (but not the zipper itself).
  inline T *Unlink(void) {
    auto old_curr = curr;
    *curr_ptr = curr->next;  // Invalidates `curr_cache` in `LinkedListZipper`.
    curr = nullptr;  // Invalidates this `LinkedListZipperElement` element.
    return old_curr;
  }

  inline T *Get(void) {
    return curr;
  }

 private:
  LinkedListZipperElement(void) = delete;

  T **curr_ptr;
  T *curr;
};

}  // namespace detail

// Zipper for in-place modification of singly-linked lists.
template <typename T>
class LinkedListZipper {
 public:
  typedef detail::LinkedListZipperElement<T> Element;
  typedef LinkedListZipper<T> Iterator;

  LinkedListZipper(void)
      : curr_ptr(nullptr),
        curr_cache(nullptr) {}

  explicit LinkedListZipper(T **list)
      : curr_ptr(list),
        curr_cache(nullptr) {}

  inline Element operator*(void) const {
    curr_cache = *curr_ptr;
    return Element(curr_ptr, curr_cache);
  }

  bool operator!=(const Iterator &that) const {
    T *curr(nullptr);
    T *that_curr(nullptr);
    if (curr_ptr != that.curr_ptr) {
      if (curr_ptr) {
        curr = *curr_ptr;
      }
      if (that.curr_ptr) {
        that_curr = *(that.curr_ptr);
      }
      return curr != that_curr;
    } else {
      return false;
    }
  }

  void operator++(void) {
    auto curr = *curr_ptr;
    if (curr) {
      if (curr_cache == curr) {
        curr_ptr = &(curr->next);  // Advance, unlinking hasn't occurred.
      }
    } else {
      curr_ptr = nullptr;
    }
    curr_cache = nullptr;
  }

  inline Iterator begin(void) const {
    return *this;
  }

  inline Iterator end(void) const {
    return Iterator(nullptr);
  }

 private:
  T **curr_ptr;
  mutable T *curr_cache;
};

}  // namespace granary

#endif  // GRANARY_BASE_LIST_H_
