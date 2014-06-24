/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef TEST_INDEX_H_
#define TEST_INDEX_H_

#include <gmock/gmock.h>

#define GRANARY_INTERNAL

#include "granary/index.h"

namespace granary {
class BlockMetaData;
}  // namespace granary

class MockIndex : public granary::IndexInterface {
 public:
  MockIndex(void) = default;
  virtual ~MockIndex(void) = default;

  // Perform a lookup operation in the code cache index. Lookup operations might
  // not return exact matches, as hinted at by the `status` field of the
  // `IndexFindResponse` structure. This has to do with block unification.
  MOCK_METHOD1(Request, granary::IndexFindResponse(granary::BlockMetaData *));

  // Insert a block into the code cache index.
  MOCK_METHOD1(Insert, void(granary::BlockMetaData *));

  GRANARY_DISALLOW_COPY_AND_ASSIGN(MockIndex);
};

#endif  // TEST_INDEX_H_
