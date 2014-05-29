/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CODE_ENCODE_H_
#define GRANARY_CODE_ENCODE_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/code/fragment.h"

namespace granary {

// Forward declaration.
class CodeCacheInterface;

// Encodes the fragments into the specified code caches.
void Encode(FragmentList *frags, CodeCacheInterface *block_cache,
            CodeCacheInterface *edge_cache);

}  // namespace granary

#endif  // GRANARY_CODE_ENCODE_H_
