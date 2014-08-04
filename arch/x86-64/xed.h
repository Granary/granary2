/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef ARCH_X86_64_XED_H_
#define ARCH_X86_64_XED_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wdocumentation"
#pragma clang diagnostic ignored "-Wswitch-enum"
extern "C" {
#define XED_DLL
#include "dependencies/xed2-intel64/include/xed-interface.h"
}  // extern C

namespace granary {
namespace arch {

struct FlagsSet {
  xed_flag_set_t read;
  xed_flag_set_t written;
};

}  // namespace arch
}  // namespace granary

#pragma clang diagnostic pop

#endif  // ARCH_X86_64_XED_H_
