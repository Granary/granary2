/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_BASE_STRING_H_
#define GRANARY_BASE_STRING_H_

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

extern void *granary_memcpy(void *, const void *, unsigned long);
extern void *granary_memset(void *, int, unsigned long);
extern int granary_memcmp(const void *, const void *, unsigned long);

#ifdef __cplusplus
}  // extern C
#endif  // __cplusplus

#define memcpy granary_memcpy
#define memset granary_memset
#define memcmp granary_memcmp


#endif  // GRANARY_BASE_STRING_H_
