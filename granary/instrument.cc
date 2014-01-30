/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"

#include "granary/cfg/control_flow_graph.h"
#include "granary/cfg/basic_block.h"
#include "granary/cfg/instruction.h"
#include "granary/environment.h"
#include "granary/instrument.h"
#include "granary/metadata.h"
#include "granary/tool.h"

namespace granary {

namespace detail {
namespace {

// Apply the instrumentation passes to the control-flow graph. First this
// instruments at the tool granularity, and then it instruments at the block
// granularity.
static void Instrument(LocalControlFlowGraph *cfg) {
  for (auto tool : Tools()) {
    tool->InstrumentCFG(cfg);
  }
  for (auto tool : Tools()) {
    tool->BeginInstrumentBB(cfg);
    for (auto block : cfg->Blocks()) {
      auto in_flight_block = DynamicCast<InFlightBasicBlock *>(block);
      if (in_flight_block) {
        tool->InstrumentBB(in_flight_block);
      }
    }
    tool->EndInstrumentBB(cfg);
  }
}

}  // namespace

// Take over a program's execution by replacing a return address with an
// instrumented return address.
void Instrument(AppProgramCounter *return_address) {
  Environment env;  // TODO(pag): Thread/CPU-private environment?

  auto meta = new GenericMetaData;

  // When attaching, we assume that the return address (`*return_address`) is
  // a native return address, and so we tell Granary that is should try ha
  auto trans = MetaDataCast<TranslationMetaData *>(meta);
  trans->translate_function_return = true;

  LocalControlFlowGraph cfg(&env, *return_address, meta);
  Instrument(&cfg);
}

}  // namespace detail
}  // namespace granary
