/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;

GRANARY_DEFINE_bool(print_stderr, false,
    "Should the printer print to stderr? The default is false: log to stdout.")

GRANARY_DEFINE_bool(print_bb_successors, false,
    "Should the printer print the successor blocks of this basic block? The "
    "default is fals: no successors will be printed.")

GRANARY_DEFINE_bool(print_bb_module, false,
    "Should the originating module of a basic block be printed? The default is "
    "false.")

GRANARY_DEFINE_bool(print_bb_offset, false,
    "If the module name/path is being printed, then also print the offset of "
    "this basic block from within the module. The default is false.")

static LogLevel kStream(LogOutput);

// Simple tool for printing out the addresses of basic blocks.
//
// TODO(pag): Relativize the printed addresses into module offsets.
class BBPrinter : public Tool {
 public:
  virtual ~BBPrinter(void) = default;

  // Instrument a basic block.
  virtual void InstrumentBB(InFlightBasicBlock *bb) {
    if (!FLAG_print_bb_module) {
      Log(kStream, "%p\n", bb->app_start_pc);
    } else {
      auto module = FindModuleByPC(bb->app_start_pc);
      if (FLAG_print_bb_offset) {
        auto offset = module->OffsetOf(bb->app_start_pc);
        Log(kStream, "%p %s:%lx\n", bb->app_start_pc, module->Name(),
            offset.offset);
      } else {
        Log(kStream, "%p %s\n", bb->app_start_pc, module->Name());
      }
    }

    if (FLAG_print_bb_successors) {
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
