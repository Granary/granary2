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

// Similar to `snprintf`. Returns the number of formatted characters.
__attribute__ ((format(printf, 3, 4)))
unsigned long Format(char *buffer, unsigned long len, const char *format, ...);

// Similar to `sscanf`. Returns the number of de-formatted arguments.
__attribute__ ((format(scanf, 2, 3)))
int DeFormat(const char *buffer, const char *format, ...);

// Compares two C strings for equality.
bool StringsMatch(const char *str1, const char *str2);

// Apply a functor to each comma-separated value. This will remove leading
// and trailing spaces.
template <unsigned long buff_len, typename F>
void ForEachCommaSeparatedString(const char *str, F functor) {
  char buffer[buff_len] = {'\0'};
  char *buff_begin = &(buffer[0]);
  char *buff_end = &(buffer[buff_len - 1]);
  char *buff_ch = &(buffer[0]);
  char *last_non_space_ch = nullptr;
  auto is_done = false;
  for (auto ch = str; !is_done; ++ch) {
    auto chr = *ch;
    is_done = '\0' == chr;
    auto is_found = is_done || ',' == chr;
    if (buff_ch < buff_end) {
      *buff_ch = chr;
      if (' ' == chr) {
        if (buff_ch > buff_begin) {
          ++buff_ch;  // Strip leading spaces.
        }
      } else if (',' == chr) {
        *buff_ch = '\0';
      } else {
        last_non_space_ch = buff_ch;
        ++buff_ch;
      }
    }
    if (is_found) {
      *buff_end = '\0';
      *buff_ch = '\0';
      if (last_non_space_ch) {
        last_non_space_ch[1] = '\0';  // Strip trailing spaces.
      }
      if (*buff_begin) {
        functor(buff_begin);
      }
      last_non_space_ch = nullptr;  // Reset.
      buff_ch = buff_begin;
    }
  }
}
}  // namespace granary
#endif

#endif  // GRANARY_BASE_STRING_H_
