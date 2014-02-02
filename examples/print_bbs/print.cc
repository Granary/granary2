/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;

GRANARY_DEFINE_bool(print_stderr, false,
    "Should the printer print to stderr? The default is false: log to stdout.")

GRANARY_DEFINE_bool(print_bb_successors, false,
    "Should the printer print the successor blocks of this basic block? The "
    "default is false: no successors is printed.")

GRANARY_DEFINE_bool(print_bb_module, false,
    "Should the originating module name/path of a basic block be printed? The "
    "default is false: no module information is printed.")

GRANARY_DEFINE_bool(print_bb_offset, false,
    "If `print_bb_module` is `true`, then also print the offset of "
    "this basic block from within the module. The default is false: no "
    "module offsets are printed.")

static LogLevel kStream(LogOutput);

// Simple tool for printing out the addresses of basic blocks.
//
// TODO(pag): Relativize the printed addresses into module offsets.
class BBPrinter : public Tool {
 public:
  virtual ~BBPrinter(void) = default;

  // Instrument a decoded basic block.
  virtual void InstrumentBlock(DecodedBasicBlock *bb) {
    auto start_pc = bb->StartPC();
    if (!FLAG_print_bb_module) {
      Log(kStream, "%p\n", start_pc);
    } else {
      auto module = FindModuleByPC(start_pc);
      if (FLAG_print_bb_offset) {
        auto offset = module->OffsetOf(start_pc);
        Log(kStream, "%p %s:%lx\n", start_pc, module->Name(), offset.offset);
      } else {
        Log(kStream, "%p %s\n", start_pc, module->Name());
      }
    }

    if (FLAG_print_bb_successors) {
      for (auto succ : bb->Successors()) {
        if (IsA<IndirectBasicBlock *>(succ.block)) {
          Log(kStream, "-> indirect\n");
        } else if (IsA<ReturnBasicBlock *>(succ.block)) {
          Log(kStream, "   return\n");
        } else {
          Log(kStream, "-> %p\n", succ.block->StartPC());
        }
      }
    }

  }
} static PRINTER;

// Initialize the `print_bbs` tool.
GRANARY_INIT({
  RegisterTool("print_bbs", &PRINTER);
  kStream = FLAG_print_stderr ? LogWarning : LogOutput;
})
