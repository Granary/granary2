/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/app.h"

namespace granary {

// Default-initializes Granary's internal module meta-data.
AppMetaData::AppMetaData(void)
    : start_pc(nullptr) {}

// Compare two translation meta-data objects for equality.
bool AppMetaData::Equals(const AppMetaData *meta) const {
  return start_pc == meta->start_pc;
}

}  // namespace granary
