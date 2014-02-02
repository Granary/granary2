/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_INIT_H_
#define GRANARY_INIT_H_

namespace granary {

enum InitKind {
  INIT_DYNAMIC,
  INIT_STATIC
};

// Initialize Granary.
void Init(InitKind kind, const char *granary_path);

}  // namespace granary

#endif  // GRANARY_INIT_H_
