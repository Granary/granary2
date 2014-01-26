/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <granary/granary.h>

using namespace granary;
namespace {

// Defines the bbcount tool.
class BBCount : public Tool {
 public:
  virtual ~BBCount(void) = default;

  virtual void InitDynamic(void) {

  }

} static TOOL;

}  // namespace

GRANARY_INIT(bbcount, {
  Log(LogOutput, "Registering `bbcount` tool.\n");
  RegisterTool(&TOOL);
})
