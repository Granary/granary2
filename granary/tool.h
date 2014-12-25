/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_TOOL_H_
#define GRANARY_TOOL_H_

#include "granary/base/base.h"
#include "granary/base/list.h"
#include "granary/base/operator.h"

#include "granary/entry.h"
#include "granary/exit.h"
#include "granary/init.h"
#include "granary/metadata.h"

namespace granary {

// Forward declarations.
class BlockFactory;
class CompensationBlock;
class DecodedBlock;
class Trace;
class InstrumentationTool;

GRANARY_INTERNAL_DEFINITION enum : size_t {
  kMaxNumTools = 64,
  kMaxToolNameLength = 32
};

// Describes the structure of tools that are used to instrument binary code.
class InstrumentationTool {
 public:
  InstrumentationTool(void);

  // Closes any open inline assembly scopes.
  virtual ~InstrumentationTool(void);

  // Initialize this tool.
  static void Init(InitReason reason);

  // Tear down this tool.
  static void Exit(ExitReason reason);

  // Used to instrument code entrypoints.
  virtual void InstrumentEntryPoint(BlockFactory *factory,
                                    CompensationBlock *entry_block,
                                    EntryPointKind kind, int category);

  // Used to instrument control-flow instructions and decide how basic blocks
  // should be materialized.
  //
  // This method is repeatedly executed until no more materialization
  // requests are made.
  virtual void InstrumentControlFlow(BlockFactory *factory,
                                     Trace *cfg);

  // Used to implement more complex forms of instrumentation where tools need
  // to see the entire local control-flow graph.
  //
  // This method is executed once per tool per instrumentation session.
  virtual void InstrumentBlocks(const Trace *cfg);

  // Used to implement the typical JIT-based model of single basic-block at a
  // time instrumentation.
  //
  // This method is executed for each decoded BB in the local CFG,
  // but is never re-executed for the same (tool, BB) pair in the current
  // instrumentation session.
  virtual void InstrumentBlock(DecodedBlock *block);

 GRANARY_PUBLIC:

  // Next tool used to instrument code.
  GRANARY_POINTER(InstrumentationTool) *next;

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstrumentationTool);
};

// Describes a generic tool.
struct ToolDescription {
  // Globally unique ID for this tool description.
  GRANARY_CONST size_t id;

  // Next offset for dependencies. Dependencies are ordered so that tool
  // ordering is consistent, regardless of global initialization order
  // (which might change from compile-to-compile).
  GRANARY_CONST size_t next_dependency_offset;

  // Is this an active instrumentation tool?
  GRANARY_CONST bool is_active;

  // Next tool.
  GRANARY_CONST ToolDescription *next;

  // Name of this tool.
  const char * GRANARY_CONST name;

  const size_t size;
  const size_t align;
  GRANARY_CONST size_t allocation_offset;

  // Virtual table of operations on tools.
  void (* const construct)(void *);
  void (* const destruct)(void *);
  void (* const init)(InitReason);
  void (* const exit)(ExitReason);
};

// Creates a description for a tool. Tool descriptions are treated as being
// constant after their `id`, `next`, and `name` fields are initialized.
template <typename T>
struct ToolDescriptor {
  static ToolDescription kDescription;
};

// Descriptor for some tool.
template <typename T>
ToolDescription ToolDescriptor<T>::kDescription = {
  0,
  1,
  false,
  nullptr,
  nullptr,
  sizeof(T),
  alignof(T),
  0,
  &(Construct<T>),
  &(Destruct<T>),
  &(T::Init),
  &(T::Exit)
};

typedef LinkedListIterator<InstrumentationTool> ToolIterator;

// Register a tool with Granary given its description.
void AddInstrumentationTool(
    ToolDescription *desc, const char *name,
    std::initializer_list<const char *> required_tools);

// Register a binary instrumenter with Granary.
template <typename T>
inline static void AddInstrumentationTool(const char *tool_name) {
  AddInstrumentationTool(&(ToolDescriptor<T>::kDescription), tool_name, {});
}

// Register a binary instrumenter with Granary.
template <typename T>
inline static void AddInstrumentationTool(
    const char *tool_name, std::initializer_list<const char *> required_tools) {
  AddInstrumentationTool(&(ToolDescriptor<T>::kDescription), tool_name,
                         required_tools);
}

#ifdef GRANARY_INTERNAL

// Initialize the tool manager.
void InitToolManager(void);

// Exit the tool manager.
void ExitToolManager(void);

// Initialize all tools. Tool initialization is typically where tools will
// register their specific their block meta-data, therefore it is important
// to initialize all tools before finalizing the meta-data manager.
void InitTools(InitReason reason);

// Exit all tools. Tool `Exit` methods should restore any global state to
// their initial values.
void ExitTools(ExitReason reason);

// Allocates all tools, and returns a pointer to the first tool allocated.
InstrumentationTool *AllocateTools(void);

// Frees all tools, given a pointer to the first tool allocated.
void FreeTools(InstrumentationTool *tools);

#endif  // GRANARY_INTERNAL

}  // namespace granary

#endif  // GRANARY_TOOL_H_
