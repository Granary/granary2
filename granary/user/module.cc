/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include <unistd.h>
#include <fcntl.h>

#include "granary/base/base.h"
#include "granary/base/string.h"
#include "granary/breakpoint.h"
#include "granary/module.h"
#include "granary/logging.h"

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

// Parse the `/proc/<pid>/maps` file for information about mapped modules.
static void ParseMapsFile(Lexer * const lexer) {
  const char *new_line_or_path(nullptr);
  for (;;) {
    auto address_range = lexer->NextToken();
    if (!address_range[0]) {
      break;
    }

    Log(LogOutput, "Info: \n");
    Log(LogOutput, "  range <%s>\n", address_range);
    auto perms = lexer->NextToken();
    Log(LogOutput, "  perms <%s>\n", perms);
    auto offset = lexer->NextToken();
    Log(LogOutput, "  offset <%s>\n", offset);
    lexer->NextToken();  // dev.
    lexer->NextToken();  // inode.
    new_line_or_path = lexer->NextToken();
    if ('\n' != new_line_or_path[0]) {
      auto path = new_line_or_path;
      Log(LogOutput, "  path <%s>\n", path);
      new_line_or_path = lexer->NextToken();  // new line.
    }
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
