/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_WATCHPOINTS_TYPE_ID_H_
#define CLIENTS_WATCHPOINTS_TYPE_ID_H_

#include <granary.h>

enum {
  kMaxWatchpointTypeId = std::numeric_limits<uint16_t>::max() >> 1U
};

// Returns the type id for some `(return address, allocation size)` pair.
uint64_t TypeIdFor(uintptr_t ret_address, size_t num_bytes);

// Returns the type id for some `(return address, allocation size)` pair.
inline static uint64_t TypeIdFor(granary::AppPC ret_address, size_t num_bytes) {
  return TypeIdFor(reinterpret_cast<uintptr_t>(ret_address), num_bytes);
}

// Apply a function to every type.
void ForEachType(std::function<void(uint64_t type_id,
                                    granary::AppPC ret_address,
                                    size_t size_order)> func);

#endif  // CLIENTS_WATCHPOINTS_TYPE_ID_H_
