/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_ENVIRONMENT_H_
#define GRANARY_ENVIRONMENT_H_

#ifndef GRANARY_INTERNAL
# error "This code is internal to Granary."
#endif

#include "granary/base/base.h"
#include "granary/base/types.h"

#include "granary/code/allocate.h"

#include "granary/context.h"
#include "granary/metadata.h"
#include "granary/module.h"
#include "granary/tool.h"

namespace granary {

// Pulls together all aspects of an instrumentation environment.
//
// This is basically every that a `Context` instance actually needs, all tied
// up in a single spot. The separation between `Context` and `Environment`
// mostly exists to not pull so many headers into `Context`, thus allowing it
// (from the header) to deal explicitly in terms of opaque types.
class Environment {
 public:
  Environment(void);

  void Attach(void);

  void AttachToAppPC(AppPC pc);

  // TODO(pag): What does it mean to unload an environment? How do the various
  //            tools learn of that?

 private:

  ModuleManager module_manager;
  MetaDataManager metadata_manager;
  ToolManager tool_manager;
  CodeAllocator edge_cache_allocator;
  Context context;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Environment);
};

}  // namespace granary

#endif  // GRANARY_ENVIRONMENT_H_
