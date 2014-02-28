/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef TEST_CACHE_H_
#define TEST_CACHE_H_

#include <gmock/gmock.h>

#define GRANARY_INTERNAL

#include "granary/code/cache.h"

// Implements a mock Granary `CodeCache`.
class MockCodeCache : public granary::CodeCacheInterface {
 public:
  MockCodeCache(void) = default;
  virtual ~MockCodeCache(void) = default;

  // Allocate a block of code from this code cache.
  MOCK_METHOD1(AllocateBlock,
               granary::CachePC(int size));

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(MockCodeCache);
};

#endif  // TEST_CACHE_H_
