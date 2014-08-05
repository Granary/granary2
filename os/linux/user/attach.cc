/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/context.h"
#include "granary/metadata.h"
#include "granary/tool.h"
#include "granary/translate.h"

#include "os/logging.h"

namespace granary {
extern "C" {

// Hook to attach granary to a function pointer.
void granary_attach(void (**func_ptr)(void)) {
  if (auto context = GlobalContext()) {
    os::Log(os::LogOutput, "Attaching Granary.\n");
    auto func_pc = UnsafeCast<AppPC *>(func_ptr);
    auto meta = context->AllocateBlockMetaData(*func_pc);
    *func_pc = TranslateEntryPoint(context, meta, ENTRYPOINT_USER_MAIN);
  }
}
}  // extern C

}  // namespace granary
