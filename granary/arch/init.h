/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_DRIVER_INIT_H_
#define GRANARY_DRIVER_INIT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {
namespace driver {

// Initialize the driver (instruction encoder/decoder).
void Init(void);

}  // namespace driver
}  // namespace granary

#endif  // GRANARY_DRIVER_INIT_H_
