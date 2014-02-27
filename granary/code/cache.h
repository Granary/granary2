/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_CACHE_H_
#define GRANARY_CODE_CACHE_H_

#ifndef GRANARY_INTERNAL
# define "This code is internal to Granary."
#endif

#include "granary/base/new.h"
#include "granary/base/pc.h"

#include "granary/code/allocate.h"

namespace granary {

// Interface for code caches.
class CodeCacheInterface {
 public:
  CodeCacheInterface(void) = default;
  virtual ~CodeCacheInterface(void) = default;

  // Allocate a block of code from this code cache.
  virtual CachePC AllocateBlock(int size) = 0;
};

// Implementation of Granary's code caches.
class CodeCache : public CodeCacheInterface {
 public:
  explicit CodeCache(int slab_size=0);
  virtual ~CodeCache(void) = default;

  // Allocate a block of code from this code cache.
  virtual CachePC AllocateBlock(int size) override;

  GRANARY_DEFINE_NEW_ALLOCATOR(CodeCache, {
    SHARED = true,
    ALIGNMENT = GRANARY_ARCH_CACHE_LINE_SIZE
  })

 private:
  // Allocator used to allocate blocks from this code cache.
  CodeAllocator allocator;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(CodeCache);
};

}  // namespace granary

#endif  // GRANARY_CODE_CACHE_H_
