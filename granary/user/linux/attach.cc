/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/cast.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/attach.h"
#include "granary/logging.h"
#include "granary/metadata.h"
#include "granary/tool.h"
#include "granary/translate.h"

namespace granary {
namespace {

ContextInterface *attach_context = nullptr;

}  // namespace
extern "C" {
void granary_attach(void (**func_ptr)(void)) {
  if (!attach_context) {
    Log(LogOutput, "Could not attach Granary.\n");
    return;
  } else {
    Log(LogOutput, "Attaching Granary.\n");
    auto func_pc = UnsafeCast<AppPC *>(func_ptr);
    *func_pc = Translate(attach_context, *func_pc, TRANSLATE_STACK_VALID);
  }
}
}  // extern C

void Attach(ContextInterface *context) {
#ifndef GRANARY_STANDALONE
  attach_context = context;
#else
  GRANARY_UNUSED(context);
#endif  // GRANARY_STANDALONE
}

}  // namespace granary
