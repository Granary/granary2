/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/cfg/iterator.h"
#include "granary/code/assemble.h"

#include "granary/logging.h"

namespace granary {

namespace {


}  // namespace

void Assemble(FragmentList fragment_list) {
  for (auto fragment : fragment_list.Fragments()) {
    GRANARY_UNUSED(fragment);
  }
}

}  // namespace granary
