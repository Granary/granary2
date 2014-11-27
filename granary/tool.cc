/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/cfg/instruction.h"

#include "granary/breakpoint.h"
#include "granary/context.h"
#include "granary/metadata.h"
#include "granary/tool.h"

#include "os/logging.h"
#include "os/module.h"

GRANARY_DEFINE_string(tools, "",
    "Comma-seprated list of tools to dynamically load on start-up. "
    "For example: `--tools=print_bbs,follow_jumps`.");

namespace granary {

// Iterator to loop over tool instances.
typedef LinkedListIterator<ToolDescription> ToolDescriptionIterator;

namespace {

// Unique ID assigned to a tool.
static std::atomic<int> gNextToolId(ATOMIC_VAR_INIT(0));

// Dependency graph between tools. If `depends_on[t1][t2]` is `true` then `t2`
// must be run before `t1` when instrumenting code.
static bool gDependsOn[kMaxNumTools][kMaxNumTools] = {{false}};

// Tools names.
static char gToolName[kMaxNumTools][kMaxToolNameLength] = {{'\0'}};

// Registered tools, indexed by ID.
static ToolDescription *gRegisteredTools[kMaxNumTools] = {nullptr};

// Find a tool's ID given its name. Returns -1 if a tool
static int ToolId(const char *name) {
  for (auto i = 0; i < kMaxNumTools; ++i) {
    if (StringsMatch(name, gToolName[i])) {
      return i;
    }
  }

  // Allocate a new ID for this tool, even if it isn't registered yet.
  auto id = gNextToolId.fetch_add(1);
  GRANARY_ASSERT(kMaxNumTools > id);
  CopyString(&(gToolName[id][0]), kMaxToolNameLength, name);
  return id;
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
InstrumentationTool::InstrumentationTool(void)
    : next(nullptr),
      context(context) {
  GRANARY_ASSERT(nullptr != context);
}
#pragma clang diagnostic pop

// Closes any open inline assembly scopes.
InstrumentationTool::~InstrumentationTool(void) {}

// Initialize this tool.
void InstrumentationTool::Init(InitReason) {}

// Exit this tool.
void InstrumentationTool::Exit(ExitReason) {}

// Used to instrument code entrypoints.
void InstrumentationTool::InstrumentEntryPoint(BlockFactory *,
                                               CompensationBasicBlock *,
                                               EntryPointKind, int) {}

// Used to instrument control-flow instructions and decide how basic blocks
// should be materialized.
//
// This method is repeatedly executed until no more materialization
// requests are made.
void InstrumentationTool::InstrumentControlFlow(BlockFactory *,
                                                LocalControlFlowGraph *) {}

// Used to implement more complex forms of instrumentation where tools need to
// see the entire local control-flow graph.
//
// This method is executed once per tool per instrumentation session.
void InstrumentationTool::InstrumentBlocks(const LocalControlFlowGraph *) {}

// Used to implement the typical JIT-based model of single basic-block at a
// time instrumentation.
//
// This method is executed for each decoded BB in the local CFG,
// but is never re-executed for the same (tool, BB) pair in the current
// instrumentation session.
void InstrumentationTool::InstrumentBlock(DecodedBasicBlock *) {}

// Initialize an empty tool manager.
InstrumentationManager::InstrumentationManager(Context *context_)
    : max_align(0),
      max_size(0),
      is_finalized(false),
      num_registered(0),
      allocator(),
      context(context_) {
  memset(&(is_registered[0]), 0, sizeof is_registered);
  memset(&(descriptions[0]), 0, sizeof descriptions);
}

InstrumentationManager::~InstrumentationManager(void) {
  allocator.Destroy();
}

// Register a tool given its name.
void InstrumentationManager::Add(const char *name) {
  GRANARY_ASSERT(!is_finalized);
  if (auto desc = gRegisteredTools[ToolId(name)]) {
    Register(desc);
    max_size = GRANARY_MAX(max_size, desc->size);
    max_align = GRANARY_MAX(max_align, desc->align);
  }
}

// Register a tool with this manager using the tool's description.
void InstrumentationManager::Register(const ToolDescription *desc) {
  if (!is_registered[desc->id]) {
    is_registered[desc->id] = true;
    for (auto required_id = 0; required_id < kMaxNumTools; ++required_id) {
      if (gDependsOn[desc->id][required_id]) {
        if (auto required_desc = gRegisteredTools[required_id]) {
          Register(required_desc);
        }
      }
    }
    descriptions[num_registered++] = desc;
  }
}

// Allocate all the tools managed by this `ToolManager` instance, and chain
// then into a linked list.
InstrumentationTool *InstrumentationManager::AllocateTools(void) {
  if (GRANARY_UNLIKELY(!is_finalized)) {
    InitAllocator();
  }
  InstrumentationTool *tools(nullptr);
  if (max_size) {
    InstrumentationTool **next_tool(&tools);
    for (auto desc : descriptions) {
      if (!desc) {
        break;
      } else {
        auto mem = allocator->Allocate();
        auto tool = reinterpret_cast<InstrumentationTool *>(mem);
        tool->context = context;  // Initialize before constructing!
        desc->initialize(mem);
        GRANARY_ASSERT(context == tool->context);

        *next_tool = tool;
        next_tool = &(tool->next);
      }
    }
  }
  return tools;
}

// Free a tool.
void InstrumentationManager::FreeTools(InstrumentationTool *tool) {
  GRANARY_ASSERT(!!is_finalized == !!tool);
  for (InstrumentationTool *next_tool(nullptr); tool; tool = next_tool) {
    next_tool = tool->next;
    tool->~InstrumentationTool();
    allocator->Free(tool);
  }
}

// Initialize the allocator for meta-data managed by this manager.
void InstrumentationManager::InitAllocator(void) {
  if (max_size) {
    auto size = GRANARY_ALIGN_TO(max_size, max_align);
    auto offset = GRANARY_ALIGN_TO(sizeof(internal::SlabList), max_align);
    auto remaining_size = internal::SLAB_ALLOCATOR_SLAB_SIZE_BYTES - offset;
    auto max_num_allocs = (remaining_size - size + 1) / size;
    auto max_offset = offset + max_num_allocs * size;
    allocator.Construct(offset, max_offset, size, size);
    is_finalized = true;
  }
}

// Registers a tool description with Granary. This assigns the tool an ID if
// it hasn't already got an ID, and then adds the tool into the global list of
// all registered tools.
void AddInstrumentationTool(
    ToolDescription *desc, const char *name,
    std::initializer_list<const char *> required_tools) {
  auto &id(desc->id);
  if (-1 == id) {
    id = ToolId(name);
    desc->id = id;
    desc->name = name;
    gRegisteredTools[id] = desc;
  } else {
    GRANARY_ASSERT(id == desc->id);
    GRANARY_ASSERT(StringsMatch(desc->name, name));
  }

  // Add in the dependencies. This might end up allocating ids for tool
  // descriptions that have yet to be loaded. This is because the initialization
  // order of the static constructors is a priori undefined.
  for (auto tool_name : required_tools) {
    if (tool_name) {
      auto required_id = ToolId(tool_name);
      GRANARY_ASSERT(!gDependsOn[required_id][id]);
      gDependsOn[id][required_id] = true;
    }
  }
}

}  // namespace granary
