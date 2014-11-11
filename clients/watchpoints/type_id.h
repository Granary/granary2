/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_WATCHPOINTS_TYPE_ID_H_
#define CLIENTS_WATCHPOINTS_TYPE_ID_H_

#include <granary.h>

// Returns the type id for some `(return address, allocation size)` pair.
uint64_t TypeIdFor(uintptr_t ret_address, size_t num_bytes);

#endif  // CLIENTS_WATCHPOINTS_TYPE_ID_H_
