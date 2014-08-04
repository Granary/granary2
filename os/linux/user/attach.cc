/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/attach.h"
#include "granary/metadata.h"
#include "granary/tool.h"
#include "granary/translate.h"

#include "os/logging.h"

namespace granary {
namespace {

ContextInterface *attach_context = nullptr;

}  // namespace
extern "C" {
void granary_attach(void (**func_ptr)(void)) {
  if (!attach_context) {
    os::Log(os::LogOutput, "Could not attach Granary.\n");
    return;
  } else {
    os::Log(os::LogOutput, "Attaching Granary.\n");
    auto func_pc = UnsafeCast<AppPC *>(func_ptr);
    *func_pc = Translate(attach_context, *func_pc, TRANSLATE_STACK_VALID);
  }
}
}  // extern C

void Attach(ContextInterface *context) {
  attach_context = context;
}

}  // namespace granary
