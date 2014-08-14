/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_OPTION_H_
#define GRANARY_BASE_OPTION_H_

#include "granary/base/base.h"
#include "granary/base/list.h"

#define GRANARY_FLAG_NAME(name) GRANARY_CAT(FLAG_, name)
#define GRANARY_HAS_FLAG_NAME(name) GRANARY_CAT(HAS_FLAG_, name)
#define GRANARY_INTERNAL_FLAG_NAME(name) GRANARY_CAT(INTERNAL_FLAG_, name)

#define GRANARY_REGISTER_OPTION(name, parser, docstring) \
  static granary::Option GRANARY_CAT(OPTION_, name) = { \
      nullptr, \
      GRANARY_TO_STRING(name), \
      &granary::detail::parser, \
      reinterpret_cast<void *>(&GRANARY_INTERNAL_FLAG_NAME(name)), \
      &GRANARY_HAS_FLAG_NAME(name), \
      docstring \
  }; \
  __attribute__((constructor(101), used)) \
  static void GRANARY_CAT(RegisterOption_, name)(void) { \
    granary::detail::RegisterOption(&GRANARY_CAT(OPTION_, name)); \
  }

#define GRANARY_DEFINE_string(name, default_value, docstring) \
  GRANARY_DECLARE_string(name); \
  namespace { \
  static const char *GRANARY_INTERNAL_FLAG_NAME(name) = (default_value); \
  GRANARY_REGISTER_OPTION(name, ParseStringOption, docstring) \
  } \
  bool GRANARY_HAS_FLAG_NAME(name) = false; \
  const char *&GRANARY_FLAG_NAME(name)(GRANARY_INTERNAL_FLAG_NAME(name))

#define GRANARY_DECLARE_string(name) \
  extern bool GRANARY_HAS_FLAG_NAME(name); \
  extern const char *&GRANARY_FLAG_NAME(name)

#define GRANARY_DEFINE_bool(name, default_value, docstring) \
  GRANARY_DECLARE_bool(name); \
  namespace { \
  bool GRANARY_INTERNAL_FLAG_NAME(name) = (default_value); \
  GRANARY_REGISTER_OPTION(name, ParseBoolOption, docstring) \
  } \
  bool GRANARY_HAS_FLAG_NAME(name) = false; \
  bool &GRANARY_FLAG_NAME(name)(GRANARY_INTERNAL_FLAG_NAME(name))

#define GRANARY_DECLARE_bool(name) \
  extern bool GRANARY_HAS_FLAG_NAME(name); \
  extern bool &GRANARY_FLAG_NAME(name)

#define GRANARY_DEFINE_positive_int(name, default_value, docstring) \
  GRANARY_DECLARE_positive_int(name); \
  namespace { \
  static int GRANARY_INTERNAL_FLAG_NAME(name) = (default_value); \
  GRANARY_REGISTER_OPTION(name, ParsePositiveIntOption, docstring) \
  } \
  bool GRANARY_HAS_FLAG_NAME(name) = false; \
  int &GRANARY_FLAG_NAME(name)(GRANARY_INTERNAL_FLAG_NAME(name))

#define GRANARY_DECLARE_positive_int(name) \
  extern bool GRANARY_HAS_FLAG_NAME(name); \
  extern int &GRANARY_FLAG_NAME(name)

namespace granary {

// Backing structure for describing command-line options to Granary.
struct Option {
  Option *next;
  const char * const name;
  void (* const parse)(Option *);
  void * const value;
  bool *has_value;
  const char * const docstring;
};

typedef LinkedListIterator<Option> OptionIterator;

// Initialize the options from an environment variable.
GRANARY_INTERNAL_DEFINITION void InitOptions(const char *env);

// Initialize the options from the command-line arguments.
GRANARY_INTERNAL_DEFINITION void InitOptions(int argc, const char **argv);

// Works for `--help` option: print out each options along with their document.
GRANARY_INTERNAL_DEFINITION void PrintAllOptions(void);

namespace detail {

// Initialize an option.
void RegisterOption(Option *option);

// Parse an option that is a string.
void ParseStringOption(Option *option);

// Parse an option that will be interpreted as a boolean value.
void ParseBoolOption(Option *option);

// Parse an option that will be interpreted as an unsigned integer but stored
// as a signed integer.
void ParsePositiveIntOption(Option *option);

}  // namespace detail
}  // namespace granary

#endif  // GRANARY_BASE_OPTION_H_
