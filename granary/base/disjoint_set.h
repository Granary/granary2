/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_DISJOINT_SET_H_
#define GRANARY_BASE_DISJOINT_SET_H_

#include "granary/base/base.h"

namespace granary {

// An embedded disjoint set data structure that implements the union-find
// algorithm.
template <typename T>
class DisjointSet {
 public:
  typedef DisjointSet<T> SelfT;

  inline DisjointSet(void)
      : parent(this),
        value() {}

  inline explicit DisjointSet(T default_value_)
      : parent(this),
        value(default_value_) {}

  // Union together two containers.
  template <typename U>
  inline void Union(U *this_container, U *that_container) {
    auto this_cont_addr = reinterpret_cast<uintptr_t>(this_container);
    auto that_cont_addr = reinterpret_cast<uintptr_t>(that_container);
    auto this_addr = reinterpret_cast<uintptr_t>(this);
    auto offset = this_addr - this_cont_addr;
    auto that = reinterpret_cast<DisjointSet *>(that_cont_addr + offset);
    Union(that);
  }

  // Find the container associated with the "root" of this set.
  template <typename U>
  inline U *Find(U *this_container) {
    auto this_cont_addr = reinterpret_cast<uintptr_t>(this_container);
    auto this_addr = reinterpret_cast<uintptr_t>(this);
    auto offset = this_addr - this_cont_addr;
    auto that = reinterpret_cast<uintptr_t>(Find());
    return reinterpret_cast<T *>(that - offset);
  }

  // Union together two potentially disjoint sets into one larger set.
  void Union(SelfT *that) {
    auto this_root = Find();
    auto that_root = that->Find();
    if (this_root->value) {
      that_root->parent = this_root;
    } else if (that_root->value) {
      this_root->parent = that_root;
    } else if (this_root < that_root) {
      that_root->parent = this_root;
    } else if (this_root > that_root) {
      this_root->parent = that_root;
    }
  }

  // Union together two potentially disjoint sets into one larger set.
  void Union(SelfT &that) {
    Union(&that);
  }

  // Find the "root" of this set.
  SelfT *Find(void) const {
    if (parent != this) {
      parent = parent->Find();
    }
    return parent;
  }

  // Returns true if two disjoint sets are the same.
  bool operator==(const SelfT &that) const {
    return Find() == that.Find();
  }

  // Returns true if two disjoint sets are different.
  bool operator!=(SelfT &that) {
    return Find() != that.Find();
  }

  inline T &Value(void) {
    return Find()->value;
  }

  inline const T &Value(void) const {
    return Find()->value;
  }

  inline T *operator->(void) {
    return &Value();
  }

  inline const T *operator->(void) const {
    return &Value();
  }

 private:
  mutable SelfT *parent;
  mutable T value;
};

}  // namespace granary

#endif  // GRANARY_BASE_DISJOINT_SET_H_
