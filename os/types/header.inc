
#ifndef GRANARY_OS_TYPE_INCLUDES
# define GRANARY_OS_TYPE_INCLUDES
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _GNU_SOURCE
# undef _GNU_SOURCE
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wreserved-user-defined-literal"
#pragma clang diagnostic ignored "-Wextern-c-compat"
#pragma clang diagnostic ignored "-Wextra-semi"

#ifdef GRANARY_WHERE_kernel
typedef bool K_Bool;
#endif
