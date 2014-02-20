/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/option.h"
#include "granary/code/allocate.h"
#include "granary/environment.h"

GRANARY_DEFINE_non_negative_int(edge_cache_slab_size, 1,
    "The number of pages allocated at once to store edge code. Each "
    "environment maintains its own edge code allocator. The default value is "
    "1 pages per slab.")

namespace granary {

// Initialize the environment.
Environment::Environment(void)
    : edge_code_allocator(new CodeAllocator(FLAG_edge_cache_slab_size)) {}

}  // namespace granary
