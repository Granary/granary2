/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include <unistd.h>
#include <fcntl.h>

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
  explicit Lexer(int fd_)
      : fd(fd_),
        file_offset(0),
        token_offset(0),
        try_fill_buffer(true) {
    FillBuffer();
  }

  ~Lexer(void) {
    close(fd);
  }

  // Get the next token in the stream. Can be called recursively to build up
  // tokens across file buffers.
  const char *NextToken(void) {
    for (; file_offset < BUFF_SIZE; ) {
      if (!file_buffer[file_offset]) {  // Done.
        return FinalizeToken();

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
    file_offset = 0;
    if (try_fill_buffer) {
      auto amount_read = read(fd, &(file_buffer), BUFF_SIZE);
      try_fill_buffer = amount_read == BUFF_SIZE;
      if (0 <= amount_read) {
        file_buffer[amount_read] = '\0';
        return true;
      }
    }
    return false;
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

// Return a file descriptor for `/proc/<pid>/maps`.
static int OpenMapsFile(void) {
  char name_buffer[Module::MAX_NAME_LEN] = {'\0'};
  Format(name_buffer, 100, "/proc/%u/maps", getpid());
  auto fd = open(&(name_buffer[0]), O_RDONLY);
  granary_break_on_fault_if(-1 == fd);
  return fd;
}

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

// Get the module kind given a module path and the previous kind.
static ModuleKind KindFromPath(const char *path, ModuleKind *prev_kind) {
  char name_buffer[Module::MAX_NAME_LEN] = {'\0'};
  auto kind = *prev_kind;

  if (ModuleKind::SHARED_LIBRARY == kind) {
    auto name = PathToName(path, name_buffer);
    if (StringsMatch("granary", name)) {
      kind = ModuleKind::GRANARY;
    } else if (FindTool(name)) {
      kind = ModuleKind::GRANARY_TOOL;
    }
  }

  // Pretend that all non-PROGRAM kinds are shared libraries until otherwise
  // determined.
  *prev_kind = ModuleKind::SHARED_LIBRARY;

  return kind;
}

// Parse the `/proc/<pid>/maps` file for information about mapped modules.
static void ParseMapsFile(Lexer * const lexer) {
  auto kind = ModuleKind::PROGRAM;

  for (;;) {
    uintptr_t base_addr(0);
    uintptr_t limit_addr(0);
    unsigned module_perms(0);
    uintptr_t module_offset(0);
    const char *module_path(nullptr);

    auto address_range = lexer->NextToken();
    if (!DeFormat(address_range, "%lx-%lx", &base_addr, &limit_addr)) {
      break;
    }

    auto perms = lexer->NextToken();
    module_perms |= 'r' == perms[0] ? detail::MODULE_READABLE : 0;
    module_perms |= 'w' == perms[1] ? detail::MODULE_WRITABLE : 0;
    module_perms |= 'x' == perms[2] ? detail::MODULE_EXECUTABLE : 0;
    module_perms |= 'p' == perms[3] ? detail::MODULE_COPY_ON_WRITE : 0;

    DeFormat(lexer->NextToken(), "%lx", &module_offset);

    lexer->NextToken();  // dev.
    lexer->NextToken();  // inode.
    module_path = lexer->NextToken();

    // TODO(pag): Do something about non-executable memory?
    if ('\n' == module_path[0]) {
      continue;
    } else if (!(module_perms & detail::MODULE_EXECUTABLE)) {
      lexer->NextToken();  // new line.
      continue;
    }

    auto module = FindModule(module_path);
    if (!module) {
      module = new Module(KindFromPath(module_path, &kind), module_path);
    }

    module->AddRange(base_addr, limit_addr, module_offset, module_perms);
    lexer->NextToken();  // new line.
  };
}

}  // namespace

// Initialize the module tracker.
void InitModules(InitKind kind) {
  granary_break_on_fault_if(InitKind::DYNAMIC != kind); // TODO(pag): Implement.

  Lexer lexer(OpenMapsFile());
  ParseMapsFile(&lexer);
}

}  // namespace granary
