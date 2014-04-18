/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_STRING_H_
#define GRANARY_BASE_STRING_H_

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

extern void *granary_memcpy(void * __restrict,
                            const void * __restrict,
                            unsigned long);
extern void *granary_memset(void *, int, unsigned long);
extern int granary_memcmp(const void * __restrict,
                          const void * __restrict, unsigned long);

#ifdef __cplusplus
}  // extern C
#endif  // __cplusplus

#ifdef memcpy
# undef memcpy
#endif

#ifdef memset
# undef memset
#endif

#ifdef memcmp
# undef memcmp
#endif

#define memcpy granary_memcpy
#define memset granary_memset
#define memcmp granary_memcmp

#ifdef __cplusplus
#include "granary/base/base.h"

namespace granary {

// Range-based for loop compatible iterator for reading characters from a
// `NUL`-terminated string of an unknown size.
class NULTerminatedStringIterator {
 public:
  inline NULTerminatedStringIterator(void)
      : ch(EOS) {}

  inline explicit NULTerminatedStringIterator(const char *ch_)
      : ch(ch_) {
    if (ch && !*ch) {
      ch = EOS;
    }
  }

  inline NULTerminatedStringIterator begin(void) const {
    return *this;
  }

  inline NULTerminatedStringIterator end(void) const {
    return NULTerminatedStringIterator();
  }

  inline void operator++(void) {
    if (!*++ch) {
      ch = EOS;
    }
  }

  inline bool operator!=(const NULTerminatedStringIterator &that) const {
    return ch != that.ch;
  }

  inline char operator*(void) const {
    return *ch;
  }

 private:
  const char *ch;

  static const char *EOS;
};

// Generic method for safely writing into a character buffer of a known size.
class WriteBuffer {
 public:
  template <unsigned long kLen>
  inline explicit WriteBuffer(char (&buffer_)[kLen])
      : buffer(buffer_),
        buffer_begin(buffer_),
        buffer_end(buffer_ + kLen),
        buffer_end_logical(kLen > 0 ? (buffer_ + (kLen - 1)) : buffer_) {}

  // Initialize the buffer. The `len` specifies the total length of the buffer,
  // included a terminating `NUL` character. Therefore, the maximum possible
  // value returned by `WriteBuffer::NumCharsWritten` is `len - 1` if `len` is
  // positive.
  inline WriteBuffer(char *buffer_, unsigned long len)
      : buffer(buffer_),
        buffer_begin(buffer_),
        buffer_end(buffer_ + len),
        buffer_end_logical(len > 0 ? (buffer_ + (len - 1)) : buffer_) {}

  // Returns true if a character can be written to the buffer and also be
  // expected to be readable from the finalized buffer.
  inline bool CanWrite(void) const {
    return buffer < buffer_end_logical;
  }

  // Write a character into the buffer. This is a NOP if the buffer is full.
  inline bool Write(char ch) {
    if (CanWrite()) {
      *buffer++ = ch;
      return true;
    } else {
      return false;
    }
  }

  // Write a string to into the buffer. This will silently not write all bytes
  // of `str` if the buffer becomes full.
  inline bool Write(const char *str) {
    auto ret = CanWrite();
    for (auto ch : NULTerminatedStringIterator(str)) {
      ret = Write(ch);
    }
    return ret;
  }

  // Returns the number of non-`NUL` characters written into the buffer.
  //
  // Note: Writing to the buffer always operates within the bounds of
  //       [buffer_begin, buffer_end_logical), so we don't need to
  inline unsigned long NumCharsWritten(void) const {
    auto ret = static_cast<unsigned long>(buffer - buffer_begin);
    return buffer_end == buffer && buffer_end > buffer_begin ? ret - 1 : ret;
  }

  // Finalize the buffer. This adds terminating `NUL` characters to the internal
  // buffer.
  inline void Finalize(void) {
    if (GRANARY_LIKELY(buffer_end_logical < buffer_end)) {
      *buffer_end_logical = '\0';
      if (buffer < buffer_end_logical) {
        *buffer = '\0';
      }
    }
  }

 private:
  WriteBuffer(void) = delete;

  char * buffer;
  char * const buffer_begin;
  char * const buffer_end;
  char * const buffer_end_logical;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(WriteBuffer);
};

// Returns the length of a C string, i.e. the number of non-'\0' characters up
// to but excluding the first '\0' character.
unsigned long StringLength(const char *ch);

// Copy at most `buffer_len` characters from the C string `str` into `buffer`.
// Ensures that `buffer` is '\0'-terminated. Assumes `buffer_len > 0`.
//
// Note: This
unsigned long CopyString(char * __restrict buffer, unsigned long buffer_len,
                         const char * __restrict str);

// Compares two C strings for equality.
bool StringsMatch(const char *str1, const char *str2);

// Similar to `vsnprintf`. Returns the number of formatted characters.
unsigned long VarFormat(char * __restrict buffer, unsigned long len,
                        const char * __restrict format, va_list args);

// Similar to `snprintf`. Returns the number of formatted characters.
__attribute__ ((format(printf, 3, 4)))
unsigned long Format(char * __restrict buffer, unsigned long len,
                     const char * __restrict format, ...);

// Similar to `sscanf`. Returns the number of de-formatted arguments.
__attribute__ ((format(scanf, 2, 3)))
int DeFormat(const char * __restrict buffer,
             const char * __restrict format, ...);

// Represents a fixed-length C-string. This is appropriate for returning a
// temporary string of a maximum length from a function.
//
// kLen represents the maximum length of the C-string, absent the NUL-
// terminator.
template <unsigned long kLen>
class FixedLengthString {
 public:
  static_assert(kLen > 0,
      "The length of a fixed-length string must be at least `1`.");

  FixedLengthString(void)
      : len(0) {
    str[0] = '\0';
  }

  inline char &operator[](unsigned long i) {
    if (i < kLen) {
      len = i;
      return str[i];
    } else {
      i = kLen;
      return str[kLen];
    }
  }

  operator const char *(void) {
    str[kLen] = '\0';
    return &(str[0]);
  }

  inline unsigned long MaxLength(void) const {
    return kLen;
  }

  inline char *Buffer(void) {
    return &(str[0]);
  }

  inline unsigned long RemainingLength(void) const {
    return kLen - len;
  }

  inline char *RemainingBuffer(void) {
    return &(str[len]);
  }

  // Similar to `snprintf`.
  __attribute__ ((format(printf, 2, 3)))
  void Format(const char * __restrict format, ...) {
    va_list args;
    va_start(args, format);
    len = VarFormat(Buffer(), MaxLength(), format, args);
    va_end(args);
  }

  // Similar to `snprintf`. This appends to the end of the string
  __attribute__ ((format(printf, 2, 3)))
  void UpdateFormat(const char * __restrict format, ...) {
    va_list args;
    va_start(args, format);
    len += VarFormat(RemainingBuffer(), RemainingLength(), format, args);
    va_end(args);
  }

 private:
  char str[kLen + 1];
  unsigned long len;
};

// Apply a functor to each comma-separated value. This will remove leading
// and trailing spaces.
template <unsigned long kBufferLen, typename F>
void ForEachCommaSeparatedString(const char *str, F functor) {
  if (!str) {
    return;
  } else {
    char buff[kBufferLen] = {'\0'};
    do {
      // Skip leading spaces and commas.
      for (; *str && (' ' == *str || ',' == *str); ++str) {}
      char *last_space = nullptr;
      for (auto i = 0UL; *str && ',' != *str; ++str, ++i) {
        if (i < (kBufferLen - 1)) {
          buff[i] = *str;
          buff[i + 1] = '\0';
          last_space = ' ' == buff[i] ? &(buff[i]) : nullptr;
        }
      }
      if (buff[0]) {
        if (last_space) {
          *last_space = '\0';
        }
        functor(buff);
      }
      buff[0] = '\0';
    } while (*str);
  }
}
}  // namespace granary
#endif

#endif  // GRANARY_BASE_STRING_H_
