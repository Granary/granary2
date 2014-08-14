/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef OS_ANNOTATE_H_
#define OS_ANNOTATE_H_

#include "granary/base/pc.h"

namespace granary {

class BlockFactory;
class DecodedBasicBlock;
class NativeInstruction;

namespace os {

// Annotate an application instruction.
void AnnotateAppInstruction(BlockFactory *factory, DecodedBasicBlock *block,
                            NativeInstruction *instr, AppPC next_pc);

}  // namespace os
}  // namespace granary

#endif  // OS_ANNOTATE_H_
