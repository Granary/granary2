/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <gtest/gtest.h>

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/string.h"

using namespace granary;

TEST(StringLengthTest, CorrectLengthOfSimpleCStrings) {
  EXPECT_EQ(0, StringLength(""));
  EXPECT_EQ(1, StringLength("a"));
  EXPECT_EQ(2, StringLength("aa"));
  EXPECT_EQ(3, StringLength("aaa"));
  EXPECT_EQ(4, StringLength("aaaa"));
}

TEST(StringLengthTest, CorrectLengthEarlyNULByte) {
  EXPECT_EQ(0, StringLength("\0a"));
  EXPECT_EQ(1, StringLength("a\0a"));
  EXPECT_EQ(2, StringLength("aa\0a"));
  EXPECT_EQ(3, StringLength("aaa\0a"));
  EXPECT_EQ(4, StringLength("aaaa\0a"));
}

TEST(StringLengthTest, BadInput) {
  EXPECT_EQ(0, StringLength(nullptr));
}

TEST(CopyStringTest, BadInput) {
  CopyString(nullptr, 0, nullptr);
  CopyString(nullptr, 0, "");
}

static bool AllCharsAreExactly(const char *buff, char check, int len) {
  for (int i = 0; i < len; ++i) {
    if (check != buff[i]) {
      return false;
    }
  }
  return true;
}

struct TestBuffer {
  char before_buffer[10];
  char buffer[10];
  char after_buffer[10];
};

TEST(CopyStringTest, ShortAndLongBuffers) {
  TestBuffer x;

  memset(&x, 0, sizeof x);
  EXPECT_EQ(0, CopyString(x.buffer, 20, ""));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));
  EXPECT_TRUE(AllCharsAreExactly(x.buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(1, CopyString(x.buffer, 20, "a"));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));
  EXPECT_TRUE(AllCharsAreExactly(x.buffer, 'a', 1));
  EXPECT_TRUE(AllCharsAreExactly(&(x.buffer[1]), '\0', 9));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(2, CopyString(x.buffer, 20, "aa"));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));
  EXPECT_TRUE(AllCharsAreExactly(x.buffer, 'a', 2));
  EXPECT_TRUE(AllCharsAreExactly(&(x.buffer[2]), '\0', 8));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, CopyString(x.buffer, 10, "aaaaaaaaaaaaaaaaaaaa"));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));
  EXPECT_TRUE(AllCharsAreExactly(x.buffer, 'a', 9));
  EXPECT_EQ('\0', x.buffer[9]);
}

TEST(StringsMatchTest, TestStringsMatch) {
  EXPECT_TRUE(StringsMatch(nullptr, nullptr));

  char buffer[10];

  EXPECT_EQ(0, CopyString(buffer, 10, ""));
  EXPECT_TRUE(StringsMatch("", ""));
  EXPECT_TRUE(StringsMatch(buffer, ""));
  EXPECT_TRUE(StringsMatch("", buffer));

  EXPECT_EQ(1, CopyString(buffer, 10, "a"));
  EXPECT_TRUE(StringsMatch("a", "a"));
  EXPECT_TRUE(StringsMatch(buffer, "a"));
  EXPECT_TRUE(StringsMatch("a", buffer));

  EXPECT_EQ(2, CopyString(buffer, 10, "aa"));
  EXPECT_TRUE(StringsMatch("aa", "aa"));
  EXPECT_TRUE(StringsMatch(buffer, "aa"));
  EXPECT_TRUE(StringsMatch("aa", buffer));
}

TEST(StringsMatchTest, TestStringsDontMatch) {
  EXPECT_FALSE(StringsMatch(nullptr, ""));
  EXPECT_FALSE(StringsMatch("", nullptr));
  EXPECT_FALSE(StringsMatch("", "a"));
  EXPECT_FALSE(StringsMatch("a", ""));
  EXPECT_FALSE(StringsMatch("a", "aa"));
  EXPECT_FALSE(StringsMatch("aa", "a"));
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat"

TEST(FormatTest, BasicFormatting) {
  TestBuffer x;

  memset(&x, 0, sizeof x);
  EXPECT_EQ(1, Format(x.buffer, 10, "%%"));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "%"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(0, Format(x.buffer, 10, "%"));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, ""));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(1, Format(x.buffer, 10, "%a"));  // Unsupported format specifier.
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "a"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(1, Format(x.buffer, 10, "%%%"));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "%"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(2, Format(x.buffer, 10, "%%%%"));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "%%"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "0123456789aaaaa"));  // Too long!
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "012345678"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));
}

TEST(FormatTest, FormatChar) {
  TestBuffer x;

  memset(&x, 0, sizeof x);
  EXPECT_EQ(1, Format(x.buffer, 10, "%c", 'a'));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "a"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "aaaaaaaa%c", 'a'));  // 8 `a`s + `a`.
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "aaaaaaaaa"));  // 9 `a`s
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "aaaaaaaaa%c", 'a'));  // 9 `a`s + `a`.
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "aaaaaaaaa"));  // 9 `a`s
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "aaaaaaaaaa%c", 'a'));  // 10 `a`s + `a`.
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "aaaaaaaaa"));  // 9 `a`s
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));
}

TEST(FormatTest, FormatPositiveInt) {
  TestBuffer x;

  memset(&x, 0, sizeof x);
  EXPECT_EQ(1, Format(x.buffer, 10, "%d", 0));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "0"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(1, Format(x.buffer, 10, "%d", 1));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "1"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(2, Format(x.buffer, 10, "%d", 10));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "10"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(3, Format(x.buffer, 10, "%d", 100));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "100"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "%d", 2147483647));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "214748364"));  // Too many chars!
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "aaaaaaaaa%d", 123)); // 9 `a`s.
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "aaaaaaaaa"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "aaaaaaaa%d", 123)); // 8 `a`s.
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "aaaaaaaa1"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "aaaaaaa%d", 123)); // 7 `a`s.
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "aaaaaaa12"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "aaaaaa%d", 123)); // 6 `a`s.
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "aaaaaa123"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));
}

TEST(FormatTest, FormatNegativeInt) {
  TestBuffer x;

  memset(&x, 0, sizeof x);
  EXPECT_EQ(1, Format(x.buffer, 10, "%d", -0));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "0"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(2, Format(x.buffer, 10, "%d", -1));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "-1"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(3, Format(x.buffer, 10, "%d", -10));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "-10"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(4, Format(x.buffer, 10, "%d", -100));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "-100"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "aaaaaaaa%d", -1)); // 8 `a`s and `-`.
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "aaaaaaaa-"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "aaaaaaaaa%d", -1)); // 9 `a`s and `-`.
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "aaaaaaaaa"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "%d", static_cast<int>(-2147483648)));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "-21474836"));  // Too many chars!
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));
}

TEST(FormatTest, FormatUnsigned) {
  TestBuffer x;

  memset(&x, 0, sizeof x);
  EXPECT_EQ(1, Format(x.buffer, 10, "%u", 0U));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "0"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(2, Format(x.buffer, 10, "%u", 99U));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "99"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));
}

TEST(FormatTest, FormatHex) {
  TestBuffer x;

  memset(&x, 0, sizeof x);
  EXPECT_EQ(1, Format(x.buffer, 10, "%x", 0U));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "0"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(6, Format(x.buffer, 10, "%x", 0xABC999));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "abc999"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));
}

TEST(FormatTest, FormatPointer) {
  TestBuffer x;

  memset(&x, 0, sizeof x);
  EXPECT_EQ(5, Format(x.buffer, 10, "%p", nullptr));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "(nil)"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(7, Format(x.buffer, 10, "%p", reinterpret_cast<void *>(0xABCDEUL)));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "0xabcde"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));
}

TEST(FormatTest, FormatStringTest) {
  TestBuffer x;

  memset(&x, 0, sizeof x);
  EXPECT_EQ(0, Format(x.buffer, 10, "%s", ""));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(AllCharsAreExactly(x.buffer, '\0', 10));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(1, Format(x.buffer, 10, "%s", "a"));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "a"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "%s", "aaaaaaaaaa"));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "aaaaaaaaa"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "%s", "aaaaaaaaaaa"));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "aaaaaaaaa"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "%saaaaaaaaa", ""));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "aaaaaaaaa"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "%saaaaaaaaa", "b"));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "baaaaaaaa"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "%saaaaaaaaa", "bbbbbbbbb"));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "bbbbbbbbb"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "ccccc%saaaaa", ""));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "cccccaaaa"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));

  memset(&x, 0, sizeof x);
  EXPECT_EQ(9, Format(x.buffer, 10, "ccccc%saaaaa", "b"));
  EXPECT_TRUE(AllCharsAreExactly(x.before_buffer, '\0', 10));
  EXPECT_TRUE(StringsMatch(x.buffer, "cccccbaaa"));
  EXPECT_TRUE(AllCharsAreExactly(x.after_buffer, '\0', 10));
}

#pragma clang diagnostic pop

// TODO(pag): Issue #2: Test `DeFormat`.

TEST(ForEachCommaSeparatedStringTest, Check) {
  ForEachCommaSeparatedString<10>(nullptr, [](const char *) {
    ASSERT_TRUE(false);
  });

  ForEachCommaSeparatedString<10>("", [](const char *) {
    ASSERT_TRUE(false);
  });

  ForEachCommaSeparatedString<10>(",", [](const char *) {
    ASSERT_TRUE(false);
  });

  ForEachCommaSeparatedString<10>(",,", [](const char *) {
    ASSERT_TRUE(false);
  });
  ForEachCommaSeparatedString<10>(", ,", [](const char *) {
    ASSERT_TRUE(false);
  });
  ForEachCommaSeparatedString<10>(" , ", [](const char *) {
    ASSERT_TRUE(false);
  });
  ForEachCommaSeparatedString<10>("a", [](const char *buff) {
    EXPECT_TRUE(StringsMatch(buff, "a"));
  });
  ForEachCommaSeparatedString<10>("a ", [](const char *buff) {
    EXPECT_TRUE(StringsMatch(buff, "a"));
  });
  ForEachCommaSeparatedString<10>(" a", [](const char *buff) {
    EXPECT_TRUE(StringsMatch(buff, "a"));
  });
  ForEachCommaSeparatedString<10>(" a ", [](const char *buff) {
    EXPECT_TRUE(StringsMatch(buff, "a"));
  });
}
