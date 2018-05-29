/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_UTIL_TYPES_H_
#define CLIENTS_UTIL_TYPES_H_

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"

#ifdef GRANARY_OS_linux
# ifdef GRANARY_WHERE_kernel
#   include "generated/linux_kernel/types.h"
# else
#   include "generated/linux_user/types.h"
# endif
#else
# error "Unrecognized operating system."
#endif

#pragma clang diagnostic pop

#undef I  // Complex I.

#endif  // CLIENTS_UTIL_TYPES_H_
