/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_STANDALONE

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
  GRANARY_PATH_LEN = 1024,
  LD_LIBRARY_PATH_LEN = 1024,
  LD_PRELOAD_LEN = 256,
  ARGS_LEN = 1024
};

static char GRANARY_PATH[GRANARY_PATH_LEN] = {'\0'};
static char LD_LIBRARY_PATH[LD_LIBRARY_PATH_LEN] = {'\0'};
static char LD_PRELOAD[LD_PRELOAD_LEN] = {'\0'};
static char ARGS[ARGS_LEN] = {'\0'};

// Get the path to the injector executable.
static const char *GetPath(const char *exec_name) {
  realpath(exec_name, GRANARY_PATH);
  char *last_slash = NULL;
  char *curr = GRANARY_PATH;
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
  const char *ld_path = getenv("LD_LIBRARY_PATH");
  unsigned long index = 0UL;
  if (ld_path) {
    index = snprintf(LD_LIBRARY_PATH, LD_LIBRARY_PATH_LEN, "%s:", ld_path);
  }
  snprintf(
      &(LD_LIBRARY_PATH[index]),
      LD_LIBRARY_PATH_LEN - index,
      "%s",
      GetPath(exec_name));

  setenv("LD_LIBRARY_PATH", LD_LIBRARY_PATH, 1);
}

// Add `libgranary.so` to the
static void SetPreload(void) {
  const char *preloads = getenv("LD_PRELOAD");
  unsigned long index = 0UL;
  if (preloads) {
    index = snprintf(LD_PRELOAD, LD_PRELOAD_LEN, "%s ", preloads);
  }
  snprintf(&(LD_PRELOAD[index]), LD_PRELOAD_LEN - index, "libgranary.so");
  setenv("LD_PRELOAD", LD_PRELOAD, 1);
}

// Combine the arguments into a single string for passing as an environment
// variable to the program that will be instrumented.
static int SetArgs(int argc, const char **argv) {
  int index = 0;
  unsigned long max_len = ARGS_LEN;
  int i = 1;
  for (; i < argc; ++i) {
    if (0 != strcmp("--", argv[i])) {
      index += snprintf(&(ARGS[index]), max_len, "%s ", argv[i]);
      max_len -= index;
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
      (char * const *) argv,
      (char * const *) environ);
}

#endif  // GRANARY_STANDALONE
