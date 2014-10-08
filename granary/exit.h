/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_EXIT_H_
#define GRANARY_EXIT_H_

namespace granary {

enum ExitReason : int {
  EXIT_PROGRAM = 0,
  EXIT_DETACH = 1
};

}  // namespace granary

#endif  // GRANARY_EXIT_H_
