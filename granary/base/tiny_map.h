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

  typedef decltype(elems.begin()) VecIterator;
  typedef decltype(const_cast<const decltype(elems) *>(&elems)->begin())
      ConstVecIterator;

  // Iterator over the entries of the `TinyMap`.
  template <typename VecIteratorType>
  class IteratorImpl {
   public:
    typedef IteratorImpl<VecIteratorType> IteratorImplType;
    inline IteratorImpl(void)
        : it() {}

    inline IteratorImpl(const IteratorImplType &that)  // NOLINT
        : it(that.it) {}

    inline explicit IteratorImpl(VecIteratorType it_)
          : it(it_) {
      Advance();
    }

    inline bool operator!=(const IteratorImplType &that) const {
      return it != that.it;
    }

    inline MapPair &operator*(void) {
      return *it;
    }

    inline const MapPair operator*(void) const {
      return *it;
    }

    void operator++(void) {
      ++it;
      Advance();
    }

   private:
    void Advance(void) {
      while (it != VecIteratorType()) {
        auto &curr(*it);
        if (K() == curr.key) {
          ++it;  // Skip over empty keys.
        } else {
          break;
        }
      }
    }

    VecIteratorType it;
  };

 public:

  typedef IteratorImpl<VecIterator> Iterator;
  typedef IteratorImpl<ConstVecIterator> ConstIterator;

  inline Iterator begin(void) {
    return Iterator(elems.begin());
  }

  inline Iterator end(void) {
    return Iterator(elems.end());
  }

  inline ConstIterator begin(void) const {
    return ConstIterator(elems.begin());
  }

  inline ConstIterator end(void) const {
    return ConstIterator(elems.end());
  }

  // Iterator class for the keys of a `TinyMap`.
  class KeyIterator {
   public:
    inline KeyIterator(void)
        : it() {}

    inline explicit KeyIterator(const ConstIterator &it_)
        : it(it_) {}

    inline KeyIterator begin(void) {
      return *this;
    }

    inline KeyIterator end(void) {
      return KeyIterator();
    }

    inline void operator++(void) {
      ++it;
    }

    inline K operator*(void) const {
      return (*it).key;
    }

    inline bool operator!=(const KeyIterator &that) const {
      return it != that.it;
    }
   private:
    ConstIterator it;
  };

  // Iterator class for the values of a `TinyMap`.
  class ValueIterator {
   public:
    inline ValueIterator(void)
        : it() {}

    inline explicit ValueIterator(const Iterator &it_)
        : it(it_) {}

    inline ValueIterator begin(void) {
      return *this;
    }

    inline ValueIterator end(void) {
      return ValueIterator();
    }

    inline void operator++(void) {
      ++it;
    }

    inline V operator*(void) {
      return (*it).value;
    }

    inline bool operator!=(const ValueIterator &that) const {
      return it != that.it;
    }
   private:
    Iterator it;
  };

  KeyIterator Keys(void) const {
    return KeyIterator(begin());
  }

  ValueIterator Values(void) {
    return ValueIterator(begin());
  }

  bool Exists(K key) const {
    for (auto &entry : elems) {
      if (entry.key == key) {
        return true;
      }
    }
    return false;
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

  unsigned long Size(void) const {
    return size;
  }

 private:
  unsigned long size;

  GRANARY_DISALLOW_COPY_AND_ASSIGN_TEMPLATE(TinyMap, (K, V, kMinMapSize));
};

}  // namespace granary

#endif  // GRANARY_BASE_TINY_MAP_H_
