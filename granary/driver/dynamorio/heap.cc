/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <stdint.h>
#include <cstddef>

#include "granary/driver/dynamorio/types.h"

extern "C" {

void *dynamorio_heap_alloc(dynamorio::dcontext_t *, size_t, dynamorio::which_heap_t) {
  return nullptr;
}


void dynamorio_heap_free(dynamorio::dcontext_t *, void *, size_t, dynamorio::which_heap_t) {

}

}


