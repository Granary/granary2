/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_UTIL_TYPES_H_
#define CLIENTS_UTIL_TYPES_H_

#ifdef GRANARY_OS_linux
# ifdef GRANARY_WHERE_kernel
#   include "generated/linux_kernel/types.h"
# else
#   include "generated/linux_user/types.h"
# endif
#else
# error "Unrecognized operating system."
#endif

#endif  // CLIENTS_UTIL_TYPES_H_
