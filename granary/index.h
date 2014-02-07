/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_INDEX_H_
#define GRANARY_INDEX_H_

#ifndef GRANARY_INTERNAL
# error "Indexing is only available to internal Granary code."
#endif

namespace granary {

// Forward declarations.
class GenericMetaData;
class CachedBasicBlock;

// Look for an entry in the code cache index, and try to revive it into a basic
// block.
CachedBasicBlock *ReviveBlockFromIndex(const GenericMetaData *);

// Add some meta-data to the set.
// Remove some meta-data.

// Transactions.

// Abstraction: One large set!!
// Add meta data
// Ask if it's in the set


}  // namespace granary

#endif  // GRANARY_INDEX_H_
