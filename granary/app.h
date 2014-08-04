/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_APP_H_
#define GRANARY_APP_H_

#include "granary/metadata.h"

namespace granary {

// Application-specific meta-data that Granary maintains about all basic blocks.
class AppMetaData : public IndexableMetaData<AppMetaData> {
 public:
  // Default-initializes Granary's internal module meta-data.
  AppMetaData(void);

  // Compare two translation meta-data objects for equality.
  bool Equals(const AppMetaData *meta) const;

  // The native program counter where this block begins.
  GRANARY_CONST AppPC start_pc;
};

// Specify to Granary tools that the function to get the info about
// `AppMetaData` already exists.
GRANARY_SHARE_METADATA(AppMetaData)

}  // namespace granary

#endif  // GRANARY_APP_H_
