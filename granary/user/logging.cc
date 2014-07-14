/* Copyright 2014 Peter Goodman, all rights reserved. */

// TODO(pag): Issue #1: Refactor this code to use an output stream.

#include "granary/base/base.h"
#include "granary/base/string.h"
#include "granary/logging.h"

#include <cstdarg>

extern "C" {

extern long long granary_write(int __fd, const void *__buf, size_t __n);

}
namespace granary {
namespace {
static int OUTPUT_FD[] = {
    1,  // LogOutput
    2,  // LogWarning
    2,  // LogError
    2,  // LogFatalError
    2,  // LogDebug
    -1
};

typedef decltype('a') CharLiteral;

static char *WriteGenericInt(char *buff, uint64_t data, bool is_64_bit,
                             bool is_signed, unsigned base) {
  if (!data) {
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
      *buff++ = '-';
      data = static_cast<uint64_t>(-signed_data);
    }
  }

  uint64_t max_base(base);
  for (; data / max_base; max_base *= base) { }
  for (max_base /= base; max_base; max_base /= base) {
    const uint64_t digit(data / max_base);
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

// Log something.
int Log(LogLevel level, const char *format, ...) {
  enum {
    WRITE_BUFF_SIZE = 255
  };

  int num_written(0);
  char write_buff[WRITE_BUFF_SIZE + 1] = {'\0'};
  char *write_ch(&(write_buff[0]));
  //const char * const write_ch_end(&(write_buff[WRITE_BUFF_SIZE]));
  char * const write_ch_begin(&(write_buff[0]));

  va_list args;
  va_start(args, format);

  bool is_64_bit(false);
  bool is_signed(false);
  unsigned base(10);
  const char *sub_string(nullptr);
  uint64_t generic_int_data(0);

  for (const char *ch(format); *ch; ) {

    // Buffer parts of the string between arguments. The somewhat unusual
    // bounds comparisons are to avoid clashing with `-Wstrict-overflow`.
    while (0 <= (write_ch - write_ch_begin) &&
           WRITE_BUFF_SIZE > (write_ch - write_ch_begin) &&
           *ch && '%' != *ch) {
      *write_ch++ = *ch++;
    }

    // Output the so-far buffered string.
    if (write_ch > write_ch_begin) {
      num_written += static_cast<int>(granary_write(
          OUTPUT_FD[static_cast<unsigned>(level)],
          write_ch_begin,
          static_cast<unsigned long>(write_ch - write_ch_begin)));
      write_ch = write_ch_begin;
    }

    // If we're done, then break out.
    if (!*ch) {
      break;

    // Not an argument, it's actually a %%
    } else if ('%' == *ch && '%' == ch[1]) {
      *write_ch++ = '%';
      ch += 2;
      continue;
    }

    // ASSERT('%' == *ch);
    ++ch;

    is_64_bit = false;
    is_signed = false;
    base = 10;

  retry:
    switch (*ch) {
      case 'c':  // Character.
        *write_ch++ = static_cast<char>(va_arg(args, int));
        ++ch;
        break;

      case 's':  // String.
        sub_string = va_arg(args, const char *);
        if (sub_string) {
          num_written += static_cast<int>(granary_write(
              OUTPUT_FD[static_cast<unsigned>(level)],
              sub_string,
              StringLength(sub_string)));
        }
        ++ch;
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
      generic_int:
        if (is_64_bit) {
          generic_int_data = va_arg(args, uint64_t);
        } else {
          generic_int_data = va_arg(args, uint32_t);
        }
        write_ch = WriteGenericInt(
          write_ch, generic_int_data, is_64_bit, is_signed, base);
        ++ch;
        break;

      case 'l':  // Long (64-bit) number.
        is_64_bit = true;
        ++ch;
        goto retry;

      case 'f':  // Floats and doubles are all treated as doubles.
        *write_ch++ = 'F';
        ++ch;
        break;

      case '\0':  // End of string.
        *write_ch++ = '%';
        break;

      default:  // Unknown.
        // ASSERT(false);
        ++ch;
        break;
    }
  }

  va_end(args);

  // Output the so-far buffered string.
  if (write_ch > write_ch_begin) {
    num_written += static_cast<int>(granary_write(
        OUTPUT_FD[static_cast<unsigned>(level)],
        write_ch_begin,
        static_cast<unsigned long>(write_ch - write_ch_begin)));
  }

  return num_written;
}

}  // namespace granary
