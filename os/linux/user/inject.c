/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_NAME
# define GRANARY_NAME granary
#endif

// Convert a sequence of symbols into a string literal.
#define GRANARY_TO_STRING__(x) #x
#define GRANARY_TO_STRING_(x) GRANARY_TO_STRING__(x)
#define GRANARY_TO_STRING(x) GRANARY_TO_STRING_(x)

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
  GRANARY_PATH_LEN = 1024,
  LD_LIBRARY_PATH_LEN = 1024,
  LD_PRELOAD_LEN = 1024,
  ARGS_LEN = 1024
};

static char GRANARY_PATH[GRANARY_PATH_LEN] = {'\0'};
static char LD_PRELOAD[LD_PRELOAD_LEN] = {'\0'};
static char ARGS[ARGS_LEN] = {'\0'};

// Get the path to the injector executable.
static void InitGranaryPath(const char *exec_name) {
  realpath(exec_name, GRANARY_PATH);
  char *last_slash = NULL;
  for (char *curr = GRANARY_PATH; *curr; ++curr) {
    if ('/' == *curr) {
      last_slash = curr;
    }
  }
  if (!last_slash) {
    exit(EXIT_FAILURE);
  }
  last_slash[1] = '\0';
  sprintf(&(last_slash[1]), "lib" GRANARY_TO_STRING(GRANARY_NAME) ".so");
}


// Add `libgranary.so` to the
static void SetPreload(void) {
  const char *preloads = getenv("LD_PRELOAD");
  unsigned long index = 0UL;
  if (preloads) {
    index = snprintf(LD_PRELOAD, LD_PRELOAD_LEN, "%s ", preloads);
  }
  snprintf(&(LD_PRELOAD[index]), LD_PRELOAD_LEN - index, "%s", GRANARY_PATH);
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
  return i + 1;
}

// Pass environment variables to `libgranary.so`.
static void SetEnv(void) {
  setenv("GRANARY_OPTIONS", ARGS, 1);
  setenv("GRANARY_PATH", GRANARY_PATH, 1);
}

extern char **environ;

// Run a command under Granary's control by setting up LD_PRELOAD.
int main(int argc, const char *argv[]) {
  InitGranaryPath(argv[0]);
  SetPreload();
  SetEnv();
  argv = &(argv[SetArgs(argc, argv)]);
  if (!argv[0] || !argv[0][0]) return 0;
  return execvpe(argv[0], (char * const *) argv, (char * const *) environ);
}
