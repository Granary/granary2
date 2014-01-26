/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;

void VisitBasicBlock(ControlFlowGraph *cfg, InFlightBasicBlock *entry_block) {
  (void) cfg;
  (void) entry_block;
}

void InitDynamic(void) {
  Log(LogOutput, "Initializing bbcount.\n");
}

// TODO(pag): Make a generalized initializer that will work in both user and
//            kernel space. Have it register a tool data structure, that way
//            Granary doesn't need to hackishly look up every symbol.

