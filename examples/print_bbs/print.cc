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
class BBPrinter : public Tool {
 public:
  BBPrinter(void) {
    Log(kStream, "digraph {\n");
    Log(kStream, "node [shape=record];\n");
  }

  virtual ~BBPrinter(void) {
    Log(kStream, "}\n");
  }

  // Instrument the local control-flow graph.
  virtual void InstrumentBlocks(const LocalControlFlowGraph *cfg) {
    Log(kStream, "bb0 [color=white fontcolor=white];\n");
    Log(kStream, "bb0 -> bb%p;\n", cfg->EntryBlock()->StartAppPC());
  }

  // Instrument a decoded basic block.
  virtual void InstrumentBlock(DecodedBasicBlock *bb) {
    auto start_pc = bb->StartAppPC();
    Log(kStream, "bb%p [label=<%p", start_pc, start_pc);
    if (FLAG_print_bb_module) {
      auto module_meta = GetMetaData<ModuleMetaData>(bb);
      auto module = module_meta->source.module;
      auto offset = module_meta->source.offset;
      if (FLAG_print_bb_offset) {
        Log(kStream, " | %s | %lx\n", module->Name(), offset);
      } else {
        Log(kStream, " | %s\n", module->Name());
      }
    }
    Log(kStream, ">];\n");

    if (FLAG_print_bb_successors) {
      for (auto succ : bb->Successors()) {
        if (IsA<IndirectBasicBlock *>(succ.block)) {
          Log(kStream, "bb%p -> indirect;\n", start_pc);
        } else if (IsA<ReturnBasicBlock *>(succ.block)) {
          Log(kStream, "bb%p -> return;\n", start_pc);
        } else if (IsA<NativeBasicBlock *>(succ.block) &&
                   !succ.block->StartAppPC()) {
          Log(kStream, "bb%p -> syscall;\n", start_pc);
        } else {
          Log(kStream, "bb%p -> bb%p;\n", start_pc, succ.block->StartAppPC());
        }
      }
    }
  }
};

// Initialize the `print_bbs` tool.
GRANARY_CLIENT_INIT({
  RegisterTool<BBPrinter>("print_bbs");
  kStream = FLAG_print_stderr ? LogWarning : LogOutput;
})
