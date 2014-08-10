/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_CSTRING_H_
#define GRANARY_BASE_CSTRING_H_

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus
void *memcpy(void * __restrict, const void * __restrict, unsigned long);
void *checked_memset(void *, int, unsigned long);
void *memset(void *, int, unsigned long);
int memcmp(const void * __restrict, const void * __restrict,
                  unsigned long);
void *memmove(void *dest, const void *src, unsigned long num_bytes);
unsigned long strlen(const char *);
int strcmp(const char *, const char *);
char *strcpy(char *, const char *);
#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // GRANARY_BASE_CSTRING_H_
