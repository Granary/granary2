/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_EXIT_H_
#define GRANARY_EXIT_H_

namespace granary {

enum ExitReason : int {
  kExitProgram = 0,
  kExitDetach = 1,
  kExitTestCase
};

void Exit(ExitReason reason);

}  // namespace granary

#endif  // GRANARY_EXIT_H_
