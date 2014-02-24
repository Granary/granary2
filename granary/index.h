/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_INDEX_H_
#define GRANARY_INDEX_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {

// Forward declarations.
class BlockMetaData;
class CachedBasicBlock;

// Look for an entry in the code cache index, and try to revive it into a basic
// block.
CachedBasicBlock *ReviveBlockFromIndex(const BlockMetaData *);

// Add some meta-data to the set.
// Remove some meta-data.

// Transactions.

// Abstraction: One large set!!
// Add meta data
// Ask if it's in the set


}  // namespace granary

#endif  // GRANARY_INDEX_H_
