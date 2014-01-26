/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/options.h"
#include "granary/breakpoint.h"

namespace granary {
namespace {

enum {
  MAX_NUM_OPTIONS = 32,
  MAX_OPTIONS_LENGTH = 1024 - 1
};

// Linked list of registered options.
Option *OPTIONS = nullptr;
std::atomic<bool> OPTIONS_INITIALIZED = ATOMIC_VAR_INIT(false);

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
  granary_break_on_fault_if(length >= MAX_OPTIONS_LENGTH);
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
          granary_break_on_fault_if(!IsValidValueChar(*ch));
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
          granary_break_on_fault_if(num_options >= MAX_NUM_OPTIONS);
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

// Compares two option names and returns true if they match.
static bool OptionNamesMatch(const char *name, const char *search_name) {
  for (; *name == *search_name; ++name, ++search_name) {
    if (!*name) {
      return true;
    }
  }
  return false;
}

// Returns a pointer to the value for an option name, or a nullptr if the option
// name was not found (or if it was specified but had no value).
const char *FindValueForName(const char *name) {
  for (int i(0); i < MAX_NUM_OPTIONS && OPTION_NAMES[i]; ++i) {
    if (OptionNamesMatch(OPTION_NAMES[i], name)) {
      return OPTION_VALUES[i];
    }
  }
  return nullptr;
}

// Process the pending options. Pending options represent internal Granary
// options.
static void ProcessPendingOptions(void) {
  for (Option *option(OPTIONS); option; option = option->next) {
    option->parse(option);
  }
}

}  // namespace

// Initialize the options from an environment variable.
void InitOptions(const char *env) {
  TerminateOptionString(CopyStringIntoOptions(0, env));
  ProcessOptionString();
  OPTIONS_INITIALIZED.store(true);
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
  OPTIONS_INITIALIZED.store(true);
  ProcessPendingOptions();
}

namespace detail {

// Initialize an option.
void RegisterOption(Option *option) {

  // Client/tool options.
  if (OPTIONS_INITIALIZED.load()) {
    option->parse(option);

  // Internal Granary options.
  } else {
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
      case '1':
      case 'y':
      case 'Y':
      case 't':
      case 'T':
      case '\0':  // Special case, treat positional as setting to true.
        *reinterpret_cast<bool *>(option->value) = true;
        break;
      case '0':
      case 'n':
      case 'N':
      case 'f':
      case 'F':
        *reinterpret_cast<bool *>(option->value) = false;
        break;
      default:
        break;
    }
  }
}

}  // namespace detail
}  // namespace granary
