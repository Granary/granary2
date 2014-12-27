/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef ARCH_EXIT_H_
#define ARCH_EXIT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

namespace granary {
namespace arch {

// Exit the driver.
void Exit(void);

}
}

#endif  // ARCH_EXIT_H_
