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
// Ensures that `buffer` is '\0'-terminated. Assumes `buffer_len > 0`.
unsigned long CopyString(char *buffer, unsigned long buffer_len,
                         const char *str) {
  unsigned long i(0);
  for (; i < buffer_len && str[i]; ++i) {
    buffer[i] = str[i];
  }

  if (i < buffer_len) {
    buffer[i++] = '\0';
  } else {
    buffer[buffer_len - 1] = '\0';
  }

  return i;
}

namespace {

typedef decltype('\0') CharLiteral;

// Write an integer into a string.
static char *FormatGenericInt(char *buff, char *buff_end, uint64_t data,
                              bool is_64_bit, bool is_signed, unsigned base) {
  if (16 == base) {
    granary_break_on_fault_if((buff + 1) >= buff_end);
    *buff++ = '0';
    *buff++ = 'x';
  }

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

}  // namespace

__attribute__ ((format(printf, 3, 4)))
unsigned long Format(char *buffer, unsigned long len, const char *format, ...) {
  va_list args;
  va_start(args, format);

  bool is_64_bit(false);
  bool is_signed(false);
  unsigned base(10);
  uint64_t generic_int_data(0);

  auto buffer_begin = buffer;
  auto buffer_end = (buffer + len) - 1;

  for (; *format && buffer < buffer_end; ) {
    if ('%' != *format || '%' == format[1]) {  // Normal characters.
      if ('%' == format[1]) {
        ++format;
      }
      *buffer++ = *format++;
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
          generic_int_data = static_cast<uint64_t>(va_arg(args, uint32_t));
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
  }

  va_end(args);

  granary_break_on_fault_if(buffer > buffer_end);
  *buffer = '\0';

  return static_cast<unsigned long>(buffer - buffer_begin);
}

}  // namespace granary
