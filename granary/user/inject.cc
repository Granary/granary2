/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_STANDALONE

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <unistd.h>

static char GRANARY_PATH[1024] = {'\0'};
static char LD_LIBRARY_PATH[1024] = {'\0'};
static char LD_PRELOAD[256] = {'\0'};
static char ARGS[1024] = {'\0'};

// Get the length of an array.
template <typename T, unsigned long len>
unsigned long ArrayLength(T(&)[len]) {
  return len;
}

// Get the path to the injector executable.
static const char *GetPath(const char *exec_name) {
  realpath(exec_name, GRANARY_PATH);

  char *last_slash(nullptr);
  char *curr(GRANARY_PATH);
  for (; *curr; ++curr) {
    if ('/' == *curr) {
      last_slash = curr;
    }
  }

  if (!last_slash) {
    exit(EXIT_FAILURE);
  }

  *last_slash = '\0';
  return GRANARY_PATH;
}

// Add the path to libgranary.so to the LD_LIBRARY_PATH.
static void SetPath(const char *exec_name) {
  auto ld_path = getenv("LD_LIBRARY_PATH");
  auto index = 0UL;
  auto max_len = ArrayLength(LD_LIBRARY_PATH);
  if (ld_path) {
    index = static_cast<unsigned long>(
        snprintf(LD_LIBRARY_PATH, max_len, "%s:", ld_path));
  }
  snprintf(
      &(LD_LIBRARY_PATH[index]), max_len - index, "%s", GetPath(exec_name));

  setenv("LD_LIBRARY_PATH", LD_LIBRARY_PATH, 1);
}

// Add `libgranary.so` to the
static void SetPreload(void) {
  auto preloads = getenv("LD_PRELOAD");
  auto index = 0UL;
  auto max_len = ArrayLength(LD_PRELOAD);
  if (preloads) {
    index = static_cast<unsigned long>(
        snprintf(LD_PRELOAD, max_len, "%s ", preloads));
  }
  snprintf(&(LD_PRELOAD[index]), max_len - index, "libgranary.so");
  setenv("LD_PRELOAD", LD_PRELOAD, 1);
}

// Combine the arguments into a single string for passing as an environment
// variable to the program that will be instrumented.
static int SetArgs(int argc, const char **argv) {
  auto index = 0;
  auto max_len = ArrayLength(ARGS);
  int i = 1;
  for (; i < argc; ++i) {
    if (0 != strcmp("--", argv[i])) {
      index += snprintf(&(ARGS[index]), max_len, "%s ", argv[i]);
      max_len -= static_cast<unsigned long>(index);
    } else {
      break;
    }
  }
  setenv("GRANARY_OPTIONS", ARGS, 1);
  return i + 1;
}

// Run a command under Granary's control by setting up LD_PRELOAD.
int main(int argc, const char **argv) {
  SetPath(argv[0]);
  SetPreload();
  argv = &(argv[SetArgs(argc, argv)]);
  return execvpe(
      argv[0],
      const_cast<char * const *>(argv),
      const_cast<char * const *>(environ));
}

#endif  // GRANARY_STANDALONE
