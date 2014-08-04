/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ARCH_INIT_H_
#define GRANARY_ARCH_INIT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {
namespace arch {

// Initialize the driver (instruction encoder/decoder).
void Init(void);

}  // namespace arch
}  // namespace granary

#endif  // GRANARY_ARCH_INIT_H_
