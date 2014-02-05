/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include <fcntl.h>
#include <unistd.h>

#include "granary/base/base.h"
#include "granary/base/string.h"
#include "granary/breakpoint.h"
#include "granary/module.h"
#include "granary/tool.h"

namespace granary {
namespace {

// Tokenize a file. This splits the file byte spaces and treats new lines and
// non-whitespace sequences of characters as tokens.
class Lexer {
 public:
  explicit Lexer(void)
      : fd(open("/proc/self/maps", O_RDONLY)),
        file_offset(0),
        token_offset(0),
        try_fill_buffer(true) {
    granary_break_on_fault_if(-1 == fd);
    FillBuffer();
  }

  ~Lexer(void) {
    close(fd);
  }

  // Get the next token in the stream. Can be called recursively to build up
  // tokens across file buffers.
  const char *NextToken(void) {
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
      auto amount_read = read(fd, &(file_buffer[0]), BUFF_SIZE);
      try_fill_buffer = 0 < amount_read;
      if (amount_read < BUFF_SIZE) {
        file_buffer[amount_read] = '\0';
      }
    }
    return try_fill_buffer;
  }

  // Finalize a token.
  const char *FinalizeToken(void) {
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
static const char *PathToName(const char *path, char *name) {
  const char *after_last_slash = nullptr;
  for (; *path; ++path) {
    if ('/' == *path) {
      after_last_slash = path + 1;
    }
  }

  // Copy part of the name in.
  for (auto ch(name); *after_last_slash; ) {
    if ('.' == *after_last_slash || '-' == *after_last_slash) {
      *ch = '\0';
      break;
    }
    *ch++ = *after_last_slash++;
  }

  if ('l' == name[0] && 'i' == name[1] && 'b' == name[2]) {
    return name + 3;
  }

  return nullptr;
}

// Get the module kind given a module path and the number of modules already
// seen.
static ModuleKind KindFromPath(const char *path, int num_modules) {
  if (!num_modules) {
    return ModuleKind::PROGRAM;
  } else if ('[' == path[0]) {  // [vdso], [vsyscall], [stack], [heap].
    return ModuleKind::DYNAMIC;
  } else {
    char name_buffer[Module::MAX_NAME_LEN] = {'\0'};
    auto name = PathToName(path, name_buffer);
    if (StringsMatch("granary", name)) {
      return ModuleKind::GRANARY;
    } else if (FindTool(name)) {
      return ModuleKind::GRANARY_TOOL;
    }
  }
  return ModuleKind::SHARED_LIBRARY;
}

// Parse the `/proc/<pid>/maps` file for information about mapped modules.
static void ParseMapsFile(void) {
  Lexer lexer;
  int num_found_modules(0);

  for (;;) {
    uintptr_t module_base(0);
    uintptr_t module_limit(0);
    unsigned module_perms(0);
    uintptr_t module_offset(0);
    const char *token(nullptr);

    token = lexer.NextToken();
    if (!DeFormat(token, "%lx-%lx", &module_base, &module_limit)) {
      break;
    }

    token = lexer.NextToken();
    module_perms |= 'r' == token[0] ? internal::MODULE_READABLE : 0;
    module_perms |= 'w' == token[1] ? internal::MODULE_WRITABLE : 0;
    module_perms |= 'x' == token[2] ? internal::MODULE_EXECUTABLE : 0;
    module_perms |= 'p' == token[3] ? internal::MODULE_COPY_ON_WRITE : 0;

    DeFormat(lexer.NextToken(), "%lx", &module_offset);

    lexer.NextToken();  // dev.
    lexer.NextToken();  // inode.
    token = lexer.NextToken();
    if ('\n' == token[0]) {
      continue;
    }

    auto module = FindModuleByName(token);
    if (!module) {
      module = new Module(KindFromPath(token, num_found_modules++), token);
      RegisterModule(module);
    }

    module->AddRange(module_base, module_limit, module_offset, module_perms);
    lexer.NextToken();  // new line.
  };
}

}  // namespace

// Initialize the module tracker.
void InitModules(InitKind kind) {
  granary_break_on_fault_if(INIT_DYNAMIC != kind); // TODO(pag): Implement.
  ParseMapsFile();
}

}  // namespace granary
