/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/arch/base.h"

#include "granary/base/lock.h"
#include "granary/base/string.h"

#include "granary/breakpoint.h"
#include "granary/context.h"
#include "granary/tool.h"

namespace granary {

// Iterator to loop over tool instances.
typedef LinkedListIterator<ToolDescription> ToolDescriptionIterator;

namespace {

// Linked list of all tool descriptions.
static std::atomic<ToolDescription *> descriptions(ATOMIC_VAR_INIT(nullptr));

// Unique ID assigned to a tool.
static int next_tool_id(0);

// Lock on assigning IDs to tools.
static FineGrainedLock next_tool_id_lock;

// Dependency graph between tools. If `depends_on[t1][t2]` is `true` then `t2`
// must be run before `t1` when instrumenting code.
static const ToolDescription *depends_on[MAX_NUM_MANAGED_TOOLS]
                                        [MAX_NUM_MANAGED_TOOLS] = {{nullptr}};

// Find a tool's description given its name.
static ToolDescription *FindDescByName(const char *name) {
  auto descs = descriptions.load(std::memory_order_acquire);
  for (auto desc : ToolDescriptionIterator(descs)) {
    if (StringsMatch(name, desc->name)) {
      return desc;
    }
  }
  return nullptr;
}

// Registers a tool description with Granary. This assignes the tool an ID if
// it hasn't already got an ID, and then adds the tool into the global list of
// all registered tools.
static void RegisterToolDescription(ToolDescription *desc, const char *name) {
  FineGrainedLocked locker(&next_tool_id_lock);
  if (-1 == desc->id) {
    GRANARY_ASSERT(MAX_NUM_MANAGED_TOOLS > next_tool_id);
    desc->id = next_tool_id++;
    desc->next = descriptions.load(std::memory_order_acquire);
    desc->name = name;
    descriptions.store(desc, std::memory_order_release);
  }
}

}  // namespace

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
// Dummy implementations of the tool API, so that tools don't need to define
// every API function.
//
// Note: This uses a hack to make sure that the `metadata_manager` field is
//       initialized with whatever its current value is. The
//       `ToolManager::Allocate` makes sure these field is initialized before
//       a `Tool` derived class constructor is invoked, so that the derived
//       tool class can register tool-specific meta-data.
Tool::Tool(void)
    : next(nullptr),
      context(context) {}
#pragma clang diagnostic pop

// Used to instrument control-flow instructions and decide how basic blocks
// should be materialized.
//
// This method is repeatedly executed until no more materialization
// requests are made.
void Tool::InstrumentControlFlow(BlockFactory *, LocalControlFlowGraph *) {}

// Used to implement more complex forms of instrumentation where tools need to
// see the entire local control-flow graph.
//
// This method is executed once per tool per instrumentation session.
void Tool::InstrumentBlocks(const LocalControlFlowGraph *) {}

// Used to implement the typical JIT-based model of single basic-block at a
// time instrumentation.
//
// This method is executed for each decoded BB in the local CFG,
// but is never re-executed for the same (tool, BB) pair in the current
// instrumentation session.
void Tool::InstrumentBlock(DecodedBasicBlock *) {}

// Register some meta-data with the meta-data manager associated with this
// tool.
void Tool::RegisterMetaData(const MetaDataDescription *desc) {
  context->RegisterMetaData(const_cast<MetaDataDescription *>(desc));
}

// Initialize an empty tool manager.
ToolManager::ToolManager(void)
    : max_align(0),
      max_size(0),
      is_finalized(false),
      num_registed(0),
      allocator() {
  memset(&(is_registered[0]), 0, sizeof is_registered);
  memset(&(descriptions[0]), 0, sizeof descriptions);
}

// Register a tool given its description.
void ToolManager::Register(const char *name) {
  GRANARY_ASSERT(!is_finalized);
  auto desc = FindDescByName(name);
  if (desc) {
    is_registered[desc->id] = true;
    Register(desc);
    descriptions[num_registed++] = desc;
    max_size = GRANARY_MAX(max_size, desc->size);
    max_align = GRANARY_MAX(max_align, desc->align);
  }
}

// Register a tool with this manager using the tool's description.
void ToolManager::Register(const ToolDescription *desc) {
  if (!is_registered[desc->id]) {
    is_registered[desc->id] = true;
    for (auto required_desc : depends_on[desc->id]) {
      if (required_desc) {
        Register(required_desc);
      }
    }
    descriptions[num_registed++] = desc;
  }
}

// Allocate all the tools managed by this `ToolManager` instance, and chain
// then into a linked list.
Tool *ToolManager::AllocateTools(ContextInterface *context) {
  if (GRANARY_UNLIKELY(!is_finalized)) {
    is_finalized = true;
    InitAllocator();
  }
  Tool *tools(nullptr);
  if (max_size) {
    Tool **next_tool(&tools);
    for (auto desc : descriptions) {
      if (!desc) {
        break;
      } else {
        auto mem = allocator->Allocate();
        auto tool = reinterpret_cast<Tool *>(mem);
        tool->context = context;  // Initialize before constructing!
        desc->initialize(mem);
        VALGRIND_MALLOCLIKE_BLOCK(tool, desc->size, 0, 0);

        *next_tool = tool;
        next_tool = &(tool->next);
      }
    }
  }
  return tools;
}

// Free a tool.
void ToolManager::FreeTools(Tool *tool) {
  for (auto next_tool = tool; tool; tool = next_tool) {
    next_tool = tool->next;
    tool->~Tool();
    allocator->Free(tool);
  }
}

// Initialize the allocator for meta-data managed by this manager.
void ToolManager::InitAllocator(void) {
  if (max_size) {
    auto size = GRANARY_ALIGN_TO(max_size, max_align);
    auto offset = GRANARY_ALIGN_TO(sizeof(internal::SlabList), size);
    auto remaining_size = GRANARY_ARCH_PAGE_FRAME_SIZE - offset;
    auto max_num_allocs = remaining_size / size;
    allocator.Construct(max_num_allocs, offset, size, size);
  }
}

// Register a tool with Granary given its description.
void RegisterTool(ToolDescription *desc,
                  const char *name,
                  std::initializer_list<const char *> required_tools) {
  RegisterToolDescription(desc, name);
  for (auto tool_name : required_tools) {
    auto required_desc = FindDescByName(tool_name);
    if (required_desc) {
      GRANARY_ASSERT(!depends_on[required_desc->id][desc->id]);
      depends_on[desc->id][required_desc->id] = required_desc;
    }
  }
}

}  // namespace granary
