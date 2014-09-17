/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "closure.h"

namespace detail {

Closure::Closure(uintptr_t callback_addr_, void *data_,
                 void (*delete_data_)(void *))
    : next(nullptr),
      callback_addr(callback_addr_),
      data(data_),
      delete_data(delete_data_) {}

Closure::~Closure(void) {
  if (delete_data && data) {
    delete_data(data);
  }
}

}  // namespace detail
