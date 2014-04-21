/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"
#include "granary/base/string.h"
#include "granary/breakpoint.h"

namespace granary {

// End-of-string signal for NUL-terminated strings.
const char *NULTerminatedStringIterator::EOS = "";

// Returns the length of a C string, i.e. the number of non-'\0' characters up
// to but excluding the first '\0' character.
unsigned long StringLength(const char *ch) {
  unsigned long len(0);
  if (ch) {
    for (; *ch; ++ch) {
      ++len;
    }
  }
  return len;
}

// Copy at most `buffer_len` characters from the C string `str` into `buffer`.
// Ensures that `buffer` is '\0'-terminated. Assumes `buffer_len > 0`. Returns
// the number of characters copied, exluding the trailing '\0'.
unsigned long CopyString(char * __restrict buffer, unsigned long buffer_len,
                         const char * __restrict str) {
  if (buffer && str) {
    WriteBuffer buff(buffer, buffer_len);
    buff.Write(str);
    buff.Finalize();
    return buff.NumCharsWritten();
  } else {
    return 0;
  }
}

// Compares two C strings for equality.
bool StringsMatch(const char *str1, const char *str2) {
  if (str1 == str2) {
    return true;
  } else if (str1 && str2) {
    for (; *str1 == *str2; ++str1, ++str2) {
      if (!*str1) {
        return true;
      }
    }
  }
  return false;
}

namespace {

typedef decltype('\0') CharLiteral;

// Write an integer into a string.
static void FormatGenericInt(WriteBuffer &buff, unsigned long data,
                             unsigned long base) {
  if (!data) {
    buff.Write('0');
    return;
  }
  auto max_base = base;
  for (; data / max_base; max_base *= base) {}
  for (max_base /= base; max_base; max_base /= base) {
    auto digit = data / max_base;
    if (digit < 10) {
      buff.Write(static_cast<char>(static_cast<CharLiteral>(digit) + '0'));
    } else {
      buff.Write(static_cast<char>(static_cast<CharLiteral>(digit - 10) + 'a'));
    }
    data -= digit * max_base;
  }
}

// De-format a hexadecimal digit.
static bool DeFormatHexadecimal(char digit, unsigned long *value) {
  unsigned long increment(0);
  if ('0' <= digit && digit <= '9') {
    increment = static_cast<unsigned long>(digit - '0');
  } else if ('a' <= digit && digit <= 'f') {
    increment = static_cast<unsigned long>(digit - 'a') + 10;
  } else if ('A' <= digit && digit <= 'F') {
    increment = static_cast<unsigned long>(digit - 'A') + 10;
  } else {
    return false;
  }
  *value = (*value * 16) + increment;
  return true;
}

// Deformat a decimal digit.
static bool DeFormatDecimal(char digit, unsigned long *value) {
  if ('0' <= digit && digit <= '9') {
    *value = (*value * 10) + static_cast<unsigned long>(digit - '0');
    return true;
  }
  return false;
}

// De-format a generic integer.
static unsigned long DeFormatGenericInt(const char *buffer, void *data,
                                        bool is_64_bit, bool is_signed,
                                        unsigned long base) {
  unsigned long len(0);
  bool is_negative(false);
  if (is_signed && '-' == buffer[0]) {
    len++;
    buffer++;
    is_negative = true;
  }

  // Decode the value.
  unsigned long value(0);
  auto get_digit = (16 == base) ? DeFormatHexadecimal : DeFormatDecimal;
  for (; get_digit(buffer[0], &value); ++buffer, ++len) {}

  if (is_negative) {
    value = static_cast<unsigned long>(-static_cast<long>(value));
  }

  // Store the decoded value.
  if (is_64_bit) {
    *reinterpret_cast<uint64_t *>(data) = static_cast<uint64_t>(value);
  } else {
    if (is_signed) {
      *reinterpret_cast<int32_t *>(data) = \
          static_cast<int32_t>(static_cast<long>(value));
    } else {
      *reinterpret_cast<uint32_t *>(data) = static_cast<uint32_t>(value);
    }
  }
  return len;
}

}  // namespace

// Similar to `vsnprintf`. Returns the number of formatted characters.
unsigned long VarFormat(char * __restrict buffer, unsigned long len,
                        const char * __restrict format, va_list args) {
  if (!buffer || !format || !len) {
    return 0;
  }

  auto is_long = false;
  auto base = 10UL;
  WriteBuffer buff(buffer, len);

  enum {
    STATE_CONTINUE,
    STATE_SEEN_FORMAT_SPEC  // Seen a `%`.
  } state(STATE_CONTINUE);

  for (auto format_ch : NULTerminatedStringIterator(format)) {
    if (STATE_CONTINUE == state) {
      if ('%' == format_ch) {
        state = STATE_SEEN_FORMAT_SPEC;
        is_long = false;
        base = 10;
      } else {
        buff.Write(format_ch);
      }
    } else {
      if ('%' == format_ch) {
        buff.Write(format_ch);
      } else if ('c' == format_ch) {
        buff.Write(static_cast<char>(va_arg(args, int)));
      } else if ('s' == format_ch) {
        buff.Write(va_arg(args, const char *));
      } else if ('l' == format_ch) {  // Long un/signed integer.
        is_long = true;
        continue;  // Don't change the state.
      } else if ('d' == format_ch) {
        long generic_int(0);
        if (is_long) {
          generic_int = va_arg(args, long);
        } else {
          generic_int = static_cast<long>(va_arg(args, int));
        }
        if (0 > generic_int) {
          buff.Write('-');
          generic_int = -generic_int;
        }
        FormatGenericInt(buff, static_cast<unsigned long>(generic_int), 10);
      } else if ('u' == format_ch || 'x' == format_ch || 'p' == format_ch) {
        if ('x' == format_ch) {
          base = 16;
        } else if ('p' == format_ch) {
          base = 16;
          is_long = true;
        }
        unsigned long generic_uint(0);
        if (is_long) {
          generic_uint = va_arg(args, unsigned long);
        } else {
          generic_uint = static_cast<unsigned long>(va_arg(args, unsigned));
        }
        if ('p' == format_ch) {
          if (!generic_uint) {
            buff.Write("(nil)");
            continue;  // Don't change the state.
          } else {
            buff.Write("0x");
          }
        }
        FormatGenericInt(buff, generic_uint, base);
      } else {
        buff.Write(format_ch); // Unexpected char after `%`, elide the `%`.
      }
      state = STATE_CONTINUE;
    }
  }
  buff.Finalize();
  return buff.NumCharsWritten();
}

// Similar to `snprintf`. Returns the number of formatted characters.
__attribute__ ((format(printf, 3, 4)))
unsigned long Format(char * __restrict buffer, unsigned long len,
                     const char * __restrict format, ...) {
  va_list args;
  va_start(args, format);
  auto ret = VarFormat(buffer, len, format, args);
  va_end(args);
  return ret;
}

// Similar to `sscanf`. Returns the number of de-formatted arguments.
__attribute__ ((format(scanf, 2, 3)))
int DeFormat(const char * __restrict buffer,
             const char * __restrict format, ...) {
  va_list args;
  va_start(args, format);

  int num_args(0);
  bool is_64_bit(false);
  bool is_signed(false);
  unsigned long base(10);

  for (; buffer[0] && format[0]; ) {
    if ('%' != format[0]) {  // Treat `%%` as a single char.
      if (format[0] != buffer[0]) {
        return num_args;
      } else {
        ++buffer;
        ++format;
        continue;
      }
    } else if ('%' == format[1]) {  // match `%%` in format to `%` in buffer.
      if ('%' != buffer[0]) {
        return num_args;
      }  else {
        ++buffer;
        format += 2;
      }
    }

  retry:
    switch (*++format) {
      case 'c':  // Character.
        *(va_arg(args, char *)) = *buffer++;
        ++num_args;
        break;

      case 'd':  // Signed decimal number.
        is_signed = true;
        goto generic_int;

      case 'x':  // Unsigned hexadecimal number.
        is_signed = false;
        base = 16;
        goto generic_int;

      case 'p':  // Pointer.
        is_64_bit = true;
        base = 16;
        goto generic_int;

      case 'u':  // Unsigned number.
      generic_int: {
        auto incremental_len = DeFormatGenericInt(
            buffer, (va_arg(args, void *)), is_64_bit, is_signed, base);
        buffer += incremental_len;
        if (incremental_len) {
          ++num_args;
          break;
        } else {
          return num_args;
        }
      }

      case 'l':  // Long (64-bit) number.
        is_64_bit = true;
        goto retry;

      case '\0':  // End of string.
        break;

      default:  // Ignore.
        break;
    }

    ++format;
  }
  va_end(args);
  return num_args;
}

}  // namespace granary
