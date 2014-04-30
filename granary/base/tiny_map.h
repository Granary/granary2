/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_TINY_MAP_H_
#define GRANARY_BASE_TINY_MAP_H_

#include "granary/base/base.h"
#include "granary/base/tiny_vector.h"

#include "granary/breakpoint.h"

namespace granary {

// Simple map data structure that maps keys to values. This should only be used
// for very small maps as the lookup mechanism is a linear search through all
// entries. Tiny maps guarantee at least enough space for `kMinMapSize` entries
// without requiring dynamic allocation.
template <typename K, typename V, unsigned long kMinMapSize>
class TinyMap {
 public:
  struct MapPair {
    inline MapPair(void)
        : key(),
          value() {}

    inline explicit MapPair(K key_)
        : key(key_),
          value() {}

    K key;
    V value;
  };

  typedef TinyMap<K, V, kMinMapSize> SelfType;

  TinyMap(void)
      : elems(),
        size(0) {}

 private:
   TinyVector<MapPair, kMinMapSize> elems;

 public:

  inline auto begin(void) -> decltype(this->elems.begin()) {
    return elems.begin();
  }

  inline auto end(void) -> decltype(this->elems.end()) {
    return elems.end();
  }

  V &operator[](K key) {
    GRANARY_ASSERT(K() != key);
    MapPair *last_empty_entry(nullptr);
    for (auto &entry : elems) {
      if (entry.key == key) {
        return entry.value;
      } else if (K() == entry.key) {
        last_empty_entry = &entry;
      }
    }
    if (last_empty_entry) {  // Take over an existing unused entry.
      new (last_empty_entry) MapPair(key);
    } else {  // Add a new entry.
      last_empty_entry = &(elems.Append(MapPair(key)));
    }
    ++size;
    return last_empty_entry->value;
  }

  void Remove(K key) {
    GRANARY_ASSERT(K() != key);
    for (auto &entry : elems) {
      if (entry.key == key) {
        --size;
        entry.~MapPair();
        new (&entry) MapPair;
        return;
      }
    }
  }

 private:
  unsigned long size;

  GRANARY_DISALLOW_COPY_AND_ASSIGN_TEMPLATE(TinyMap, (K, V, kMinMapSize));
};

}  // namespace granary

#endif  // GRANARY_BASE_TINY_MAP_H_
