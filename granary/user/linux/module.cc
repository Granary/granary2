/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/breakpoint.h"
#include "granary/client.h"
#include "granary/module.h"

extern "C" {

extern int granary_open(const char *__file, int __oflag, ...);
extern int granary_close(int __fd);
extern long long granary_read(int __fd, void *__buf, size_t __nbytes);

}  // extern C
namespace granary {
namespace {

// Tokenize a file. This splits the file byte spaces and treats new lines and
// non-whitespace sequences of characters as tokens.
class Lexer {
 public:
  explicit Lexer(void)
      : fd(granary_open("/proc/self/maps", 0 /* O_RDONLY */)),
        file_offset(0),
        token_offset(0),
        try_fill_buffer(true) {
    GRANARY_ASSERT(-1 != fd);
    FillBuffer();
  }

  ~Lexer(void) {
    granary_close(fd);
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
  enum {
    BUFF_SIZE = 4096
  };

  // Fill the file buffer of the lexer.
  bool FillBuffer(void) {
    if (try_fill_buffer) {
      file_offset = 0;
      file_buffer[0] = '\0';
      auto amount_read = granary_read(fd, &(file_buffer[0]), BUFF_SIZE);
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
  char file_buffer[BUFF_SIZE];
  char token_buffer[Module::MAX_NAME_LEN];

  int fd;  // File descriptor of file being tokenized.
  int file_offset;  // Offset into the `file_buffer`.
  int token_offset;  // Offset into the `token_buffer`.
  bool try_fill_buffer;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Lexer);
};

// Returns a pointer to the name of a module. For example, we want to extract
// `acl` from `/lib/x86_64-linux-gnu/libacl.so.1.1.0`.
static const char *PathToName(char *path) {
  char *after_last_slash = nullptr;
  auto name = path;
  for (; *path; ++path) {
    if ('/' == *path) {
      after_last_slash = path + 1;
    }
  }
  if (after_last_slash) {
    name = after_last_slash;  // Update the beginning of the name.
  }
  // Truncate the name at the first period or dash (e.g. `*.so`).
  for (auto ch(name); *ch; ++ch) {
    if ('.' == *ch || '-' == *ch) {
      *ch = '\0';
      break;
    }
  }
  // If the name begins with `lib` (e.g. `libc.so`) then remove the `lib` part.
  if ('l' == name[0] && 'i' == name[1] && 'b' == name[2]) {
    name += 3;
  }
  return name;
}

// Get the module kind given a module path and the number of modules already
// seen.
static ModuleKind KindFromName(const char *name, int num_modules) {
  if (!num_modules) {
    return ModuleKind::PROGRAM;
  } else if ('[' == name[0]) {  // [vdso], [vsyscall], [stack], [heap].
    return ModuleKind::DYNAMIC;
  } else {
    if (StringsMatch(GRANARY_NAME_STRING, name)) {
      return ModuleKind::GRANARY;
    } else if (ClientIsRegistered(name)) {
      return ModuleKind::GRANARY_CLIENT;
    }
  }
  return ModuleKind::SHARED_LIBRARY;
}

// Parse the `/proc/self/maps` file for information about mapped modules.
static void ParseMapsFile(ModuleManager *manager) {
  Lexer lexer;
  int num_found_modules(0);

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
      continue;
    }

    auto name = PathToName(token);
    auto module = manager->FindByName(name);
    if (!module) {
      module = new Module(KindFromName(name, num_found_modules++), name);
      manager->Register(module);
    }

    module->AddRange(module_base, module_limit, module_offset, module_perms);
    lexer.NextToken();  // new line.
  };
}
}  // namespace

// Find all built-in modules. In user space, this will go and find things like
// libc. In kernel space, this will identify already loaded modules.
void ModuleManager::RegisterAllBuiltIn(void) {
  ParseMapsFile(this);
}

}  // namespace granary
