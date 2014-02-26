/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_TOOL_H_
#define GRANARY_TOOL_H_

#include "granary/base/base.h"
#include "granary/base/list.h"
#include "granary/base/new.h"
#include "granary/base/operator.h"

#include "granary/metadata.h"

namespace granary {

// Forward declarations.
class BlockFactory;
class LocalControlFlowGraph;
class DecodedBasicBlock;
class Tool;

GRANARY_INTERNAL_DEFINITION class ContextInterface;
GRANARY_INTERNAL_DEFINITION enum {
  MAX_NUM_MANAGED_TOOLS = 32
};

// Describes the structure of tools.
class Tool {
 public:
  Tool(void);

  virtual ~Tool(void) = default;

  // Used to instrument control-flow instructions and decide how basic blocks
  // should be materialized.
  //
  // This method is repeatedly executed until no more materialization
  // requests are made.
  virtual void InstrumentControlFlow(BlockFactory *materializer,
                                     LocalControlFlowGraph *cfg);

  // Used to implement more complex forms of instrumentation where tools need
  // to see the entire local control-flow graph.
  //
  // This method is executed once per tool per instrumentation session.
  virtual void InstrumentBlocks(const LocalControlFlowGraph *cfg);

  // Used to implement the typical JIT-based model of single basic-block at a
  // time instrumentation.
  //
  // This method is executed for each decoded BB in the local CFG,
  // but is never re-executed for the same (tool, BB) pair in the current
  // instrumentation session.
  virtual void InstrumentBlock(DecodedBasicBlock *block);

  // Register some meta-data with Granary that will be used with this tool.
  // This is a convenience method around the `RegisterMetaData` method that
  // operates directly on a meta-data description.
  template <typename T>
  inline void RegisterMetaData(void) {
    RegisterMetaData(MetaDataDescription::Get<T>());
  }

  // TODO(pag): Need to expose static methods on a tool to flush a
  //            CachedBasicBlock.

  // TODO(pag): Should there be a static methods for begin/end everything? Or
  //            something to signify an environment switch? Or maybe just
  //            something to signify a takeover event?
  //                --> Perhaps a takeover event on a module, and a release
  //                    event on a module.

 GRANARY_PUBLIC:
  // Register some meta-data with the meta-data manager associated with this
  // tool.
  void RegisterMetaData(const MetaDataDescription *desc);

  // Next tool used to instrument code.
  GRANARY_POINTER(Tool) *next;

  // Context into which this tool has been instantiated.
  GRANARY_POINTER(ContextInterface) *env;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(Tool);
};

// Describes a generic tool.
struct ToolDescription {
  // Globally unique ID for this tool description.
  GRANARY_CONST int id;

  // Next tool.
  GRANARY_CONST ToolDescription *next;

  // Name of this tool.
  const char * GRANARY_CONST name;

  const size_t size;
  const size_t align;

  // Virtual table of operations on tools.
  void (* const initialize)(void *);
};

// Creates a description for a tool.
template <typename T>
struct ToolDescriptor {
  static ToolDescription kDescription;
};

// Descriptor for some tool.
template <typename T>
ToolDescription ToolDescriptor<T>::kDescription = {
  -1,
  nullptr,
  nullptr,
  sizeof(T),
  alignof(T),
  &(Construct<T>)
};

#ifdef GRANARY_INTERNAL
typedef LinkedListIterator<Tool> ToolIterator;

// Manages a set of tools.
class ToolManager {
 public:
  // Initialize an empty tool manager.
  ToolManager(void);

  // Register a tool with this manager using the tool's name. This will look
  // up the tool in the global list of all registered Granary tools.
  void Register(const char *name);

  // Allocate all the tools managed by this `ToolManager` instance, and chain
  // then into a linked list. The returns list can be used to instrument code.
  //
  // This ensures that tools are allocated and inserted into the list according
  // to the order of their dependencies, whilst also trying to preserve the
  // tool order specified at the command-line.
  Tool *Allocate(ContextInterface *env);

  // Free some meta-data.
  void Free(Tool *tool);

 private:

  // Register a tool with this manager using the tool's description.
  void Register(const ToolDescription *desc);

  // Initialize the allocator for meta-data managed by this manager.
  void InitAllocator(void);

  // Maximum alignment and size (in bytes) of all registered tools.
  size_t max_align;
  size_t max_size;

  // Has this manager been finalized?
  bool is_finalized;

  // All tools registered with this manager.
  int num_registed;
  bool is_registered[MAX_NUM_MANAGED_TOOLS];
  const ToolDescription *descriptions[MAX_NUM_MANAGED_TOOLS];

  // TODO(pag): Have an ordered array of tool descriptions that represent the
  //            tools ordered according to how they are specified at the command
  //            line or according to internal dependencies.

  // Slab allocator for allocating tool instrumentation objects.
  Container<internal::SlabAllocator> allocator;

  GRANARY_DISALLOW_COPY_AND_ASSIGN(ToolManager);
};
#endif  // GRANARY_INTERNAL

// Register a tool with Granary given its description.
void RegisterTool(ToolDescription *desc,
                  const char *name,
                  std::initializer_list<const char *> required_tools);

// Register a tool with Granary.
template <typename T>
inline void RegisterTool(const char *tool_name) {
  RegisterTool(&(ToolDescriptor<T>::kDescription), tool_name, {});
}

// Register a tool with Granary.
template <typename T>
inline void RegisterTool(const char *tool_name,
                         std::initializer_list<const char *> required_tools) {
  RegisterTool(&(ToolDescriptor<T>::kDescription), tool_name, required_tools);
}

}  // namespace granary

#endif  // GRANARY_TOOL_H_
