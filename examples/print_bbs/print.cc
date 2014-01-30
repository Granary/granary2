/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;

GRANARY_DEFINE_bool(print_stderr, false,
    "Should the printer print to stderr? The default is false: log to stdout.")

GRANARY_DEFINE_bool(print_successors, false,
    "Should the printer print the successor blocks of this basic block? The "
    "default is fals: no successors will be printed.")

static LogLevel kStream(LogOutput);

// Simple tool for printing out the addresses of basic blocks.
//
// TODO(pag): Relativize the printed addresses into module offsets.
class BBPrinter : public Tool {
 public:
  virtual ~BBPrinter(void) = default;

  // Instrument a basic block.
  virtual void InstrumentBB(InFlightBasicBlock *bb) {
    Log(kStream, "%p\n", bb->app_start_pc);
    if (FLAG_print_successors) {
      for (auto succ : bb->Successors()) {
        if (!IsA<UnknownBasicBlock *>(succ.block)) {
          Log(kStream, "-> %p\n", succ.block->app_start_pc);
        }
      }
    }

  }
} static PRINTER;

// Initialize the `print_bbs` tool.
GRANARY_INIT(print_bbs, {
  RegisterTool("print_bbs", &PRINTER);
  kStream = FLAG_print_stderr ? LogWarning : LogOutput;
})
