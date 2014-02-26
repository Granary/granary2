/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/base/base.h"
#include "granary/base/string.h"
#include "granary/breakpoint.h"

namespace granary {

// Returns the length of a C string, i.e. the number of non-'\0' characters up
// to but excluding the first '\0' character.
unsigned long StringLength(const char *ch) {
  unsigned long len(0);
  for (; *ch; ++ch) {
    ++len;
  }
  return len;
}

// Copy at most `buffer_len` characters from the C string `str` into `buffer`.
// Ensures that `buffer` is '\0'-terminated. Assumes `buffer_len > 0`. Returns
// the number of characters copied, exluding the trailing '\0'.
unsigned long CopyString(char *buffer, unsigned long buffer_len,
                         const char *str) {
  unsigned long i(0);
  for (; i < buffer_len && str[i]; ++i) {
    buffer[i] = str[i];
  }

  buffer[buffer_len - 1] = '\0';
  if (i < buffer_len) {
    buffer[i] = '\0';
  } else {
    GRANARY_ASSERT(2 <= buffer_len);
    i = buffer_len - 2;\
  }

  return i;
}

namespace {

typedef decltype('\0') CharLiteral;

// Write an integer into a string.
static char *FormatGenericInt(char *buff, char *buff_end, unsigned long data,
                              bool is_64_bit, bool is_signed,
                              unsigned long base) {
  if (!data) {
    granary_break_on_fault_if(buff >= buff_end);
    *buff++ = '0';
    return buff;
  }

  // Sign-extend a 32-bit signed value to 64-bit.
  if (!is_64_bit && is_signed) {
    data = static_cast<uint64_t>(static_cast<int64_t>(
      static_cast<int32_t>(data & 0xFFFFFFFFULL)));
  }

  // Treat everything as 64-bit.
  if (is_signed) {
    const int64_t signed_data(static_cast<int64_t>(data));
    if (signed_data < 0) {
      granary_break_on_fault_if(buff >= buff_end);
      *buff++ = '-';
      data = static_cast<uint64_t>(-signed_data);
    }
  }

  uint64_t max_base(base);
  for (; data / max_base; max_base *= base) {}
  for (max_base /= base; max_base; max_base /= base) {
    const uint64_t digit(data / max_base);
    granary_break_on_fault_if(buff >= buff_end);
    if (digit < 10) {
      *buff++ = static_cast<char>(static_cast<CharLiteral>(digit) + '0');
    } else {
      *buff++ = static_cast<char>(static_cast<CharLiteral>(digit - 10) + 'a');
    }
    data -= digit * max_base;
  }

  return buff;
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

// Similar to `snprintf`. Returns the number of formatted characters.
__attribute__ ((format(printf, 3, 4)))
unsigned long Format(char *buffer, unsigned long len, const char *format, ...) {
  va_list args;
  va_start(args, format);

  bool is_64_bit(false);
  bool is_signed(false);
  unsigned long base(10);
  unsigned long generic_int_data(0);

  auto buffer_begin = buffer;
  auto buffer_end = (buffer + len) - 1;

  for (; *format && buffer < buffer_end; ) {
    if ('%' != *format) {  // Normal characters.
      *buffer++ = *format++;
      continue;
    } else if ('%' == format[1]) {
      *buffer++ = *format++;
      ++format;
      continue;
    }

    is_64_bit = false;
    is_signed = false;
    base = 10;

  retry:
    switch (*++format) {
      case 'c':  // Character.
        granary_break_on_fault_if(buffer >= buffer_end);
        *buffer++ = static_cast<char>(va_arg(args, int));
        break;

      case 's': {  // String.
        auto sub_string = va_arg(args, const char *);
        auto max_len = len - static_cast<unsigned long>(buffer - buffer_begin);
        buffer += CopyString(buffer, max_len, sub_string);
        break;
      }

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
      generic_int:
        generic_int_data = 0;
        if (is_64_bit) {
          generic_int_data = va_arg(args, uint64_t);
        } else {
          generic_int_data = static_cast<unsigned long>(va_arg(args, uint32_t));
        }
        buffer = FormatGenericInt(
          buffer, buffer_end, generic_int_data, is_64_bit, is_signed, base);
        break;

      case 'l':  // Long (64-bit) number.
        is_64_bit = true;
        goto retry;

      case '\0':  // End of string.
        granary_break_on_fault_if(buffer >= buffer_end);
        *buffer++ = '%';
        break;

      default:  // Unknown.
        granary_break_on_fault();
        break;
    }

    ++format;
  }

  va_end(args);

  granary_break_on_fault_if(buffer > buffer_end);
  *buffer = '\0';

  return static_cast<unsigned long>(buffer - buffer_begin);
}

// Similar to `sscanf`. Returns the number of de-formatted arguments.
__attribute__ ((format(scanf, 2, 3)))
int DeFormat(const char *buffer, const char *format, ...) {
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

      default:
        granary_break_on_fault();
        break;
    }

    ++format;
  }
  va_end(args);
  return num_args;
}

// Compares two C strings for equality.
bool StringsMatch(const char *str1, const char *str2) {
  if (str1 == str2) {
    return true;
  }
  if ((str1 && !str2) || (!str1 && str2)) {
    return false;
  }
  for (; *str1 == *str2; ++str1, ++str2) {
    if (!*str1) {
      return true;
    }
  }
  return false;
}

}  // namespace granary
