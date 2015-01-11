/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/breakpoint.h"

#include "os/module.h"

extern "C" {

extern int open(const char *__file, int __oflag, void *);
extern int close(int __fd);
extern long long read(int __fd, void *__buf, size_t __nbytes);
extern unsigned char granary_begin_text;
extern unsigned char granary_end_text;

}  // extern C
namespace granary {
namespace os {
namespace {

enum {
  BUFF_SIZE = 8192
};

// Global buffer and lock for reading `/proc/self/maps`. This isn't stack
// allocated as we don't want to unnecessarily risk blowing the stack.
static char file_buffer[BUFF_SIZE];
SpinLock file_buffer_lock;

// Tokenize a file. This splits the file byte spaces and treats new lines and
// non-whitespace sequences of characters as tokens.
class Lexer {
 public:
  explicit Lexer(void)
      : fd(open("/proc/self/maps", 0 /* O_RDONLY */, nullptr)),
        file_offset(0),
        token_offset(0),
        try_fill_buffer(true) {
    GRANARY_ASSERT(-1 != fd);
    FillBuffer();
  }

  ~Lexer(void) {
    close(fd);
  }

  // Get the next token in the stream. Can be called recursively to build up
  // tokens across file buffers.
  char *NextToken(void) {
    for (; file_offset < BUFF_SIZE; ) {
      if (!file_buffer[file_offset]) {  // Done or need to fill the buffer.
        break;

      // Break tokens at spaces and new lines, but treat new lines as tokens
      // themselves.
      } else if (' ' == file_buffer[file_offset] ||
                 '\n' == file_buffer[file_offset]) {
        if (token_offset) {  // We've read in a token already so return it.
          return FinalizeToken();
        } else if ('\n' == file_buffer[file_offset++]) {
          token_buffer[0] = '\n';
          token_offset = 1;
          return FinalizeToken(); // Treat beginning new lines as tokens.
        } else {
          continue; // We're skipping spaces before a token.
        }
      } else {
        token_buffer[token_offset++] = file_buffer[file_offset++];
      }
    }
    if (!FillBuffer()) {
      return FinalizeToken();
    }
    return NextToken();
  }

 private:

  // Fill the file buffer of the lexer.
  bool FillBuffer(void) {
    if (try_fill_buffer) {
      file_offset = 0;
      file_buffer[0] = '\0';
      auto amount_read = read(fd, &(file_buffer[0]), BUFF_SIZE);
      try_fill_buffer = 0 < amount_read;
      if (amount_read < BUFF_SIZE) {
        file_buffer[amount_read] = '\0';
      }
    }
    return try_fill_buffer;
  }

  // Finalize a token.
  char *FinalizeToken(void) {
    token_buffer[token_offset] = '\0';
    token_offset = 0;
    return &(token_buffer[0]);
  }

  // Buffers for holding data read from the file and in-progress token data.
  char token_buffer[Module::kMaxModulePathLength];

  int fd;  // File descriptor of file being tokenized.
  int file_offset;  // Offset into the `file_buffer`.
  int token_offset;  // Offset into the `token_buffer`.
  bool try_fill_buffer;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Lexer);
};

static char kAnonModuleName[] = {'[', 'a', 'n', 'o', 'n', ']', '\0'};

// Parse the `/proc/self/maps` file for information about mapped modules.
static void ParseMapsFile(ModuleManager *manager) {
  Lexer lexer;
  for (;;) {
    uintptr_t module_base(0);
    uintptr_t module_limit(0);
    unsigned module_perms(0);
    uintptr_t module_offset(0);
    char *token(nullptr);

    token = lexer.NextToken();
    if (!DeFormat(token, "%lx-%lx", &module_base, &module_limit)) {
      break;
    }

    token = lexer.NextToken();
    module_perms |= 'r' == token[0] ? MODULE_READABLE : 0;
    module_perms |= 'w' == token[1] ? MODULE_WRITABLE : 0;
    module_perms |= 'x' == token[2] ? MODULE_EXECUTABLE : 0;
    module_perms |= 'p' == token[3] ? MODULE_COPY_ON_WRITE : 0;

    DeFormat(lexer.NextToken(), "%lx", &module_offset);

    lexer.NextToken();  // dev.
    lexer.NextToken();  // inode.
    token = lexer.NextToken();
    if ('\n' == token[0]) {
      token = kAnonModuleName;
    }

    auto module = manager->FindByPath(token);;
    if (!module) {
      module = new Module(token);
      manager->Register(module);
    }

    module->AddRange(module_base, module_limit, module_offset, module_perms);

    if (kAnonModuleName == token) continue;  // It was a `\n`.

    do {
      token = lexer.NextToken();  // Skip things like `(deleted)`.
    } while ('\0' != token[0] && '\n' != token[0]);
  };
}
}  // namespace

// Find all built-in modules. In user space, this will go and find things like
// libc. In kernel space, this will identify already loaded modules.
void ModuleManager::RegisterAllBuiltIn(void) {
  SpinLockedRegion locker(&file_buffer_lock);
  ParseMapsFile(this);
}

// Find and register all built-in modules.
void ModuleManager::ReRegisterAllBuiltIn(void) {
  RegisterAllBuiltIn();
}

}  // namespace os
}  // namespace granary
