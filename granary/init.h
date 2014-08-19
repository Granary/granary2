/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_INIT_H_
#define GRANARY_INIT_H_

namespace granary {

// Runs the constructors from the initialization array.
GRANARY_INTERNAL_DEFINITION void PreInit(void);

// Initializes Granary.
GRANARY_INTERNAL_DEFINITION void Init(void);

}  // namespace granary

#endif  // GRANARY_INIT_H_
