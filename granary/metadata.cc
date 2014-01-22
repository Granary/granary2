/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL
#include "granary/metadata.h"

namespace granary {

BasicBlockMetaData *BasicBlockMetaData::Copy(void) const {
  return nullptr;  // TODO(pag): Implement this.
}

void BasicBlockMetaData::Hash(HashFunction *hasher) const {
  GRANARY_UNUSED(flags);
  GRANARY_UNUSED(hasher);  // TODO(pag): Implement this.
}

}  // namespace granary
