/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/option.h"
#include "granary/base/string.h"
#include "granary/breakpoint.h"
#include "granary/logging.h"

namespace granary {
namespace {

enum {
  MAX_NUM_OPTIONS = 32,
  MAX_OPTIONS_LENGTH = 1024 - 1
};

// Linked list of registered options.
Option *OPTIONS = nullptr;
bool OPTIONS_INITIALIZED = false;

// Copy of the option string.
static int OPTION_STRING_LENGTH = 0;
static char OPTION_STRING[MAX_OPTIONS_LENGTH + 1] = {'\0'};
static const char *OPTION_NAMES[MAX_NUM_OPTIONS] = {nullptr};
static const char *OPTION_VALUES[MAX_NUM_OPTIONS] = {nullptr};

// Copy a substring into the main options string.
static int CopyStringIntoOptions(int offset, const char *string) {
  for (; offset < MAX_OPTIONS_LENGTH && *string; ++string) {
    OPTION_STRING[offset++] = *string;
  }
  return offset;
}

// Finalize the option string.
static void TerminateOptionString(int length) {
  GRANARY_ASSERT(MAX_OPTIONS_LENGTH > length);
  OPTION_STRING[length] = '\0';
  OPTION_STRING_LENGTH = length;
}

// Check that a character is a valid option character.
static bool IsValidOptionChar(char ch) {
  return ('a' <= ch && ch <= 'z') ||
         ('A' <= ch && ch <= 'Z') ||
         ('_' == ch);
}

// Check that a character is a valid option character.
static bool IsValidValueChar(char ch) {
  return ' ' < ch && ch <= '~' && '[' != ch && ']' != ch;
}

// Format an option string into a more amenable internal format. This is a sort
// of pre-processing step to distinguish options from values.
static void ProcessOptionString(void) {
  char *ch(&OPTION_STRING[0]);
  char * const max_ch(&OPTION_STRING[OPTION_STRING_LENGTH]);
  unsigned num_options(0);

  enum {
    IN_OPTION,
    IN_VALUE,
    IN_LITERAL_VALUE,
    SEEN_EQUAL,
    SEEN_DASH,
    ELSEWHERE
  } state = ELSEWHERE;

  for (; ch < max_ch; ++ch) {
    switch (state) {
      case IN_OPTION: {
        const char ch_val = *ch;

        // Terminate the option name.
        if (!IsValidOptionChar(ch_val)) {
          state = ELSEWHERE;
          *ch = '\0';
        }

        // We've seen an equal, which mean's we're moving into the
        // beginning of a value.
        if ('=' == ch_val) {
          state = SEEN_EQUAL;
        }
        break;
      }

      case IN_VALUE:
        if (!IsValidValueChar(*ch)) {
          state = ELSEWHERE;
          *ch = '\0';
        }
        break;

      case IN_LITERAL_VALUE:
        if (']' == *ch) {
          state = ELSEWHERE;
          *ch = '\0';
        } else if (' ' != *ch) {
          GRANARY_ASSERT(IsValidValueChar(*ch));
        }
        break;

      case SEEN_EQUAL:
        if ('[' == *ch) {  // E.g. `--tools=[bbcount:pgo]`.
          *ch = '\0';
          state = IN_LITERAL_VALUE;
          OPTION_VALUES[num_options - 1] = ch + 1;
        } else if (IsValidValueChar(*ch)) {  // E.g. `--tools=bbcount`.
          state = IN_VALUE;
          OPTION_VALUES[num_options - 1] = ch;
        } else {  // E.g. `--tools=`.
          state = ELSEWHERE;
        }
        break;

      case SEEN_DASH:
        if ('-' == *ch) {
          state = IN_OPTION;
          GRANARY_ASSERT(MAX_NUM_OPTIONS > num_options);
          OPTION_VALUES[num_options] = "";  // Default to positional.
          OPTION_NAMES[num_options++] = ch + 1;
        } else {
          state = ELSEWHERE;
        }
        *ch = '\0';
        break;

      case ELSEWHERE:
        if ('-' == *ch) {
          state = SEEN_DASH;
        }
        *ch = '\0';
        break;
    }
  }
}

// Returns a pointer to the value for an option name, or a nullptr if the option
// name was not found (or if it was specified but had no value).
const char *FindValueForName(const char *name) {
  for (int i(0); i < MAX_NUM_OPTIONS && OPTION_NAMES[i]; ++i) {
    if (StringsMatch(OPTION_NAMES[i], name)) {
      return OPTION_VALUES[i];
    }
  }
  return nullptr;
}

// Process the pending options. Pending options represent internal Granary
// options.
static void ProcessPendingOptions(void) {
  for (auto option : OptionIterator(OPTIONS)) {
    option->parse(option);
  }
}

}  // namespace

// Initialize the options from an environment variable.
void InitOptions(const char *env) {
  TerminateOptionString(CopyStringIntoOptions(0, env));
  ProcessOptionString();
  OPTIONS_INITIALIZED = true;
  ProcessPendingOptions();
}

// Initialize the options from the command-line arguments.
void InitOptions(int argc, const char **argv) {
  int offset(0);
  int arg(1);
  for (const char *sep(""); arg < argc; ++arg, sep = " ") {
    offset = CopyStringIntoOptions(offset, sep);
    offset = CopyStringIntoOptions(offset, argv[arg]);
  }
  TerminateOptionString(offset);
  ProcessOptionString();
  OPTIONS_INITIALIZED = true;
  ProcessPendingOptions();
}

// Works for --help option: print out each options along with their document.
void PrintAllOptions(void) {
  Log(LogOutput, "Usage for user space: granary.out clients-and-tools-and"
      "-args\n\n");

  for (auto option : OptionIterator(OPTIONS)) {
    Log(LogOutput, "--\033[1m%s\033[m\n\t%s\n", option->name,
        option->docstring);
  }
}

namespace detail {

// Initialize an option.
void RegisterOption(Option *option) {
  if (OPTIONS_INITIALIZED) {
    option->parse(option);  // Client/tool options.
  }
  if (!option->next) {
    option->next = OPTIONS;
    OPTIONS = option;
  }
}

// Parse an option that is a string.
void ParseStringOption(Option *option) {
  auto value = FindValueForName(option->name);
  if (value) {
    *reinterpret_cast<const char **>(option->value) = value;
  }
}

// Parse an option that will be interpreted as a boolean value.
void ParseBoolOption(Option *option) {
  auto value = FindValueForName(option->name);
  if (value) {
    switch (*value) {
      case '1': case 'y': case 'Y': case 't': case 'T': case '\0':
        *reinterpret_cast<bool *>(option->value) = true;
        break;
      case '0': case 'n': case 'N': case 'f': case 'F':
        *reinterpret_cast<bool *>(option->value) = false;
        break;
      default:
        break;
    }
  }
}

// Parse an option that will be interpreted as an unsigned integer but stored
// as a signed integer.
void ParsePositiveIntOption(Option *option) {
  auto value = FindValueForName(option->name);
  if (value) {
    int int_value(0);
    DeFormat(value, "%u", reinterpret_cast<unsigned *>(&int_value));
    if (0 < int_value) {
      *reinterpret_cast<int *>(option->value) = int_value;
    }
  }
}

}  // namespace detail
}  // namespace granary
