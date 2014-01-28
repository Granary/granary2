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

#ifdef __cplusplus
namespace granary {
// Returns the length of a C string, i.e. the number of non-'\0' characters up
// to but excluding the first '\0' character.
unsigned long StringLength(const char *ch);

// Copy at most `buffer_len` characters from the C string `str` into `buffer`.
// Ensures that `buffer` is '\0'-terminated. Assumes `buffer_len > 0`.
unsigned long CopyString(char *buffer, unsigned long buffer_len,
                         const char *str);

__attribute__ ((format(printf, 3, 4)))
unsigned long Format(char *buffer, unsigned long len, const char *format, ...);

__attribute__ ((format(scanf, 2, 3)))
unsigned long DeFormat(char *buffer, const char *format, ...);

// Copies one string into another.
}  // namespace granary
#endif

#endif  // GRANARY_BASE_STRING_H_
