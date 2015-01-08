/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_EXIT_H_
#define GRANARY_EXIT_H_

namespace granary {

enum ExitReason : int {
  // This says we're completely exiting the program.
  kExitProgram = 0,

  // This says we're completely detaching from the program, but might
  // re-attach at a later time. Therefore,
  kExitDetach = 1,
  kExitThread = 2
};

void Exit(ExitReason reason);

}  // namespace granary

#endif  // GRANARY_EXIT_H_
