/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_LIST_H_
#define GRANARY_BASE_LIST_H_

#include "granary/base/base.h"
#include "granary/base/type_trait.h"
#include "granary/breakpoint.h"

namespace granary {

// Forward declaration.
template <typename T> class ListOfListHead;

template <typename T>
class ListHead {
 public:
  inline ListHead(void)
      : next(nullptr),
        prev(nullptr) {}

  T *Last(void) const {
    auto curr = this;
    for (; curr->next; curr = &(curr->next->list)) {}
    return ContainerOf(curr);
  }

  T *First(void) const {
    auto curr = this;
    for (; curr->prev; curr = &(curr->prev->list)) {}
    return ContainerOf(curr);
  }

  T *Next(void) const {
    return next;
  }

  T *Previous(void) const {
    return prev;
  }

  void SetNext(T *new_next) {
    GRANARY_ASSERT(nullptr != new_next);
    if (next) Chain(new_next->list.Last(), next);
    Chain(ContainerOf(this), new_next->list.First());
    GRANARY_ASSERT(next != ContainerOf(this));
  }

  void SetPrevious(T *new_prev) {
    GRANARY_ASSERT(nullptr != new_prev);
    if (prev) Chain(prev, new_prev->list.First());
    Chain(new_prev->list.Last(), ContainerOf(this));
    GRANARY_ASSERT(prev != ContainerOf(this));
  }

  void Unlink(void) {
    Chain(prev, next);
    next = nullptr;
    prev = nullptr;
  }

  // Returns true if this list element is attached to any other list elements.
  inline bool IsLinked(void) const {
    return nullptr != prev || nullptr != next;
  }

 private:
  GRANARY_EXTERNAL_FRIEND class ListOfListHead<T>;

  // Chain together two list heads.
  inline static void Chain(T *first, T *second) {
    if (first) first->list.next = second;
    if (second) second->list.prev = first;
  }

  // Return the object associated with this list head.
  inline static T *ContainerOf(const ListHead<T> *list) {
    GRANARY_ASSERT(nullptr != list);
    enum {
      kListHeadOffset = offsetof(T, list)
    };
    return reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(list) -
                                 kListHeadOffset);
  }

  T *next;
  T *prev;
};

// Interface for managing doubly-linked lists. Assumes that type there exists a
// pulbic `T::list` with type `ListHead`.
template <typename T>
class ListOfListHead {
 public:
  inline ListOfListHead(void)
      : first(nullptr),
        last(nullptr) {}

  inline T *First(void) const {
    GRANARY_ASSERT(!first || !first->list.prev);
    return first;
  }

  inline T *Last(void) const {
    GRANARY_ASSERT(!last || !last->list.next);
    return last;
  }

  void Prepend(T *elm) {
    GRANARY_ASSERT(nullptr != elm);
    if (first) elm->list.SetNext(first);
    if (!last) last = elm->list.Last();
    first = elm->list.First();
  }

  void Append(T *elm) {
    GRANARY_ASSERT(nullptr != elm);
    if (last) elm->list.SetPrevious(last);
    if (!first) first = elm->list.First();
    last = elm->list.Last();
  }

  void InsertBefore(T *before_elm, T *new_elm) {
    if (before_elm == first) {
      Prepend(new_elm);
    } else {
      GRANARY_ASSERT(nullptr != before_elm);
      GRANARY_ASSERT(nullptr != first);
      before_elm->list.SetPrevious(new_elm);
    }
  }

  void InsertAfter(T *after_elm, T *new_elm) {
    if (after_elm == last) {
      Append(new_elm);
    } else {
      GRANARY_ASSERT(nullptr != after_elm);
      GRANARY_ASSERT(nullptr != last);
      after_elm->list.SetNext(new_elm);
    }
  }

  void Remove(T *elm) {
    auto elm_next = elm->list.Next();
    auto elm_prev = elm->list.Previous();
    elm->list.Unlink();
    if (last == elm) last = elm_prev;
    if (first == elm) first = elm_next;
  }

 private:
  T *first;
  T *last;
};

// Generic iterator for simple linked lists where `T::list` is a public member
// of type `ListHead`.
template <typename T>
class ListHeadIterator {
 public:
  typedef ListHeadIterator<T> Iterator;

  ListHeadIterator(void)
      : curr(nullptr) {}

  ListHeadIterator(const Iterator &that)  // NOLINT
      : curr(that.curr) {}

  ListHeadIterator(const Iterator &&that)  // NOLINT
      : curr(that.curr) {}

  explicit ListHeadIterator(T *first)
      : curr(first) {}

  explicit ListHeadIterator(const ListOfListHead<T> &list)
      : curr(list.First()) {}

  explicit ListHeadIterator(const ListOfListHead<T> *list)
      : curr(list->First()) {}

  inline Iterator begin(void) const {
    return *this;
  }

  inline Iterator end(void) const {
    return Iterator(static_cast<T *>(nullptr));
  }

  inline void operator++(void) {
    curr = curr->list.Next();
  }

  inline bool operator!=(const Iterator &that) const {
    return curr != that.curr;
  }

  inline T *operator*(void) const {
    return curr;
  }

  // Returns the last valid element from an iterator.
  static T *Last(Iterator elems) {
    if (!elems.curr) return nullptr;
    return elems.curr->list.Last();
  }

  // Returns the last valid element from an iterator.
  static inline T *Last(T *elems_ptr) {
    GRANARY_ASSERT(nullptr != elems_ptr);
    return elems_ptr->list.Last();
  }

 private:
  T *curr;
};

// Generic iterator for simple linked lists where `T::list` is a public member
// of type `ListHead`.
template <typename T>
class ReverseListHeadIterator {
 public:
  typedef ReverseListHeadIterator<T> Iterator;

  ReverseListHeadIterator(void)
      : curr(nullptr) {}

  ReverseListHeadIterator(const Iterator &that)  // NOLINT
      : curr(that.curr) {}

  ReverseListHeadIterator(const Iterator &&that)  // NOLINT
      : curr(that.curr) {}

  explicit ReverseListHeadIterator(T *last)
      : curr(last) {}

  explicit ReverseListHeadIterator(const ListOfListHead<T> &list)
      : curr(list.Last()) {}

  explicit ReverseListHeadIterator(const ListOfListHead<T> *list)
      : curr(list->Last()) {}

  inline Iterator begin(void) const {
    return *this;
  }

  inline Iterator end(void) const {
    return Iterator(static_cast<T *>(nullptr));
  }

  inline void operator++(void) {
    curr = curr->list.Previous();
  }

  inline bool operator!=(const Iterator &that) const {
    return curr != that.curr;
  }

  inline T *operator*(void) const {
    return curr;
  }

  // Returns the last valid element from an iterator.
  static T *First(Iterator elems) {
    if (!elems.curr) return nullptr;
    return elems.curr->list.First();
  }

  // Returns the last valid element from an iterator.
  static inline T *First(T *elems_ptr) {
    GRANARY_ASSERT(nullptr != elems_ptr);
    return elems_ptr->list.First();
  }

 private:
  T *curr;
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
    return Iterator(static_cast<T *>(nullptr));
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
  static T *Last(Iterator elems) {
    T *last(nullptr);
    for (auto elem : elems) {
      last = elem;
    }
    return last;
  }

  // Returns the last valid element from an iterator.
  static inline T *Last(T *elems_ptr) {
    return Last(Iterator(elems_ptr));
  }

 private:
  T *curr;
};

// Generic iterator for simple linked lists with public `prev` fields.
template <typename T>
class ReverseLinkedListIterator {
 public:
  typedef ReverseLinkedListIterator<T> Iterator;

  ReverseLinkedListIterator(void)
      : curr(nullptr) {}

  ReverseLinkedListIterator(const Iterator &that)  // NOLINT
      : curr(that.curr) {}

  ReverseLinkedListIterator(const Iterator &&that)  // NOLINT
      : curr(that.curr) {}

  explicit ReverseLinkedListIterator(T *last)
      : curr(last) {}

  inline Iterator begin(void) const {
    return *this;
  }

  inline Iterator end(void) const {
    return Iterator(static_cast<T *>(nullptr));
  }

  inline void operator++(void) {
    curr = curr->prev;
  }

  inline bool operator!=(const Iterator &that) const {
    return curr != that.curr;
  }

  inline T *operator*(void) const {
    return curr;
  }

  // Returns the first valid element from an iterator.
  static T *First(Iterator elems) {
    T *last(nullptr);
    for (auto elem : elems) {
      last = elem;
    }
    return last;
  }

  // Returns the last valid element from an iterator.
  static inline T *First(T *elems_ptr) {
    return Last(Iterator(elems_ptr));
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
  inline std::unique_ptr<T> Unlink(void) {
    auto old_curr = curr;
    *curr_ptr = curr->next;  // Invalidates `curr_cache` in `LinkedListZipper`.
    curr = nullptr;  // Invalidates this `LinkedListZipperElement` element.
    return std::unique_ptr<T>(old_curr);
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
      : curr_ptr(list && *list ? list : nullptr),
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
    return Iterator(static_cast<T **>(nullptr));
  }

 private:
  T **curr_ptr;
  mutable T *curr_cache;
};

}  // namespace granary

#endif  // GRANARY_BASE_LIST_H_
