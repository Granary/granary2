/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "closure.h"

namespace detail {

Closure::Closure(uintptr_t callback_addr_)
    : next(nullptr),
      callback_addr(callback_addr_) {}

Closure::~Closure(void) {}

}  // namespace detail
