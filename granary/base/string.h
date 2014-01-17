/* Copyright 2014 Peter Goodman, all rights reserved. */


#ifndef GRANARY_BASE_STRING_H_
#define GRANARY_BASE_STRING_H_

extern "C" {
extern void *granary_memcpy(void *, const void *, unsigned long);
extern void *granary_memset(void *, int, unsigned long);
extern int granary_memcmp(const void *, const void *, unsigned long);
}  // extern C

#define memcpy granary_memcpy
#define memset granary_memset
#define memcmp granary_memcmp


#endif  // GRANARY_BASE_STRING_H_
