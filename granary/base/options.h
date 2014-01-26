/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_BASE_OPTIONS_H_
#define GRANARY_BASE_OPTIONS_H_

#include "granary/base/base.h"

#define GRANARY_FLAG_NAME(name) GRANARY_CAT(FLAG_, name)

#define GRANARY_REGISTER_OPTION(name, parser, docstring) \
  __attribute__((constructor(101), used)) \
  static void GRANARY_CAT(RegisterOption, name)(void) { \
    static granary::Option option = { \
        nullptr, \
        GRANARY_TO_STRING(name), \
        &granary::detail::parser, \
        reinterpret_cast<void *>(&GRANARY_FLAG_NAME(name)), \
        docstring \
    }; \
    granary::detail::RegisterOption(&option); \
  }

#define GRANARY_DEFINE_string(name, default_value, docstring) \
  const char *GRANARY_FLAG_NAME(name) = (default_value); \
  GRANARY_REGISTER_OPTION(name, ParseStringOption, docstring)

#define GRANARY_DECLARE_string(type, name) \
  extern const char *GRANARY_FLAG_NAME(name);

namespace granary {

// Backing structure for describing command-line options to Granary.
struct Option {
  Option *next;
  const char * const name;
  void (* const parse)(Option *);
  void * const value;
  const char * const docstring;
};

// Initialize the options from an environment variable.
GRANARY_INTERNAL_DEFINITION void InitOptions(const char *env);

// Initialize the options from the command-line arguments.
GRANARY_INTERNAL_DEFINITION void InitOptions(int argc, const char **argv);

namespace detail {

// Initialize an option.
void RegisterOption(Option *option);

// Parse an option that is a string.
void ParseStringOption(Option *option);

}  // namespace detail
}  // namespace granary

#endif  // GRANARY_BASE_OPTIONS_H_
