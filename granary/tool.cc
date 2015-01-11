/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "arch/base.h"

#include "granary/base/base.h"
#include "granary/base/new.h"
#include "granary/base/option.h"
#include "granary/base/string.h"

#include "granary/breakpoint.h"
#include "granary/tool.h"

#include "os/logging.h"

GRANARY_DEFINE_string(tools, "",
    "Comma-seprated list of tools to dynamically load on start-up. "
    "For example: `--tools=poly_code,count_bbs`.");

namespace granary {

InstrumentationTool::InstrumentationTool(void)
    : next(nullptr) {}

// Closes any open inline assembly scopes.
InstrumentationTool::~InstrumentationTool(void) {}

// Initialize this tool.
void InstrumentationTool::Init(InitReason) {}

// Tear down this tool.
void InstrumentationTool::Exit(ExitReason) {}

// Used to instrument code entrypoints.
void InstrumentationTool::InstrumentEntryPoint(BlockFactory *,
                                               CompensationBlock *,
                                               EntryPointKind, int) {}

// Used to instrument control-flow instructions and decide how basic blocks
// should be materialized.
//
// This method is repeatedly executed until no more materialization
// requests are made.
void InstrumentationTool::InstrumentControlFlow(BlockFactory *,
                                                Trace *) {}

// Used to implement more complex forms of instrumentation where tools need
// to see the entire local control-flow graph.
//
// This method is executed once per tool per instrumentation session.
void InstrumentationTool::InstrumentBlocks(Trace *) {}

// Used to implement the typical JIT-based model of single basic-block at a
// time instrumentation.
//
// This method is executed for each decoded BB in the local CFG,
// but is never re-executed for the same (tool, BB) pair in the current
// instrumentation session.
void InstrumentationTool::InstrumentBlock(DecodedBlock *) {}

namespace {

// Iterator to loop over tool instances.
typedef LinkedListIterator<ToolDescription> ToolDescriptionIterator;

static size_t gNextToolId = 0;

// Unordered array of registered tools. When a tool is registered, its
// descriptor's `id` is an index into this array. When a tool name is
// referenced before its tool is registered, then the associated slot will be
// allocated, and its name will be stored in the `id`th entry to the
// `gToolNames` array.
static ToolDescription *gRegisteredTools[kMaxNumTools] = {nullptr};

// Unordered array of tool names. When a tool is registered, its name is copied
// into here and put at the `ToolDescription::id`th entry. If a tool is
// referenced before it is registered, then its name is copied in here, and the
// name's index will serve as the tool's ID when that tool is eventually
// registered.
static char gToolNames[kMaxNumTools][kMaxToolNameLength] = {{'\0'}};

// Ordered list of active tools. The ordering respects `gToolDependencies`.
static ToolDescription *gActiveTools[kMaxNumTools] = {nullptr};
static size_t gNextActiveTool = 0;
static size_t gToolDependencies[kMaxNumTools][kMaxNumTools] = {{0}};

// The last requested tool. We use this to add extra dependency edges.
static ToolDescription *gPrevRequestedTool = nullptr;

// The total size and maximum alignment needed for all tools.
static size_t gAllocationSize = 0;
static size_t gAllocationAlign = 0;

// Slab gToolAllocator for allocating tool instrumentation objects.
static Container<internal::SlabAllocator> gToolAllocator;

// Get the name for a registered tool.
static size_t IdForName(const char *name) {
  for (auto i = 0UL; i < kMaxNumTools; ++i) {
    if (StringsMatch(name, gToolNames[i])) {
      return i;
    }
  }

  // Allocate a new ID for this tool, even if it isn't registered yet.
  auto id = gNextToolId++;
  GRANARY_ASSERT(kMaxNumTools > id);
  CopyString(&(gToolNames[id][0]), kMaxToolNameLength, name);
  return id;
}

// Get the descriptor for a tool, given the tool's name.
static ToolDescription *DescForName(const char *name) {
  for (auto i = 0UL; i < kMaxNumTools; ++i) {
    if (StringsMatch(name, gToolNames[i])) {
      return gRegisteredTools[i];
    }
  }
  return nullptr;
}

// Request that a specific tool be used for instrumentation.
static void RequestTool(const char *name) {
  auto desc = DescForName(name);
  if (!desc) {
    os::Log(os::LogDebug, "Error: Could not find requested tool `%s`.\n", name);
    return;
  }

  // Add an implicit dependency based on how tools are ordered at the
  // command-line.
  if (gPrevRequestedTool) {
    gToolDependencies[desc->id][0] = gPrevRequestedTool->id;
  }
  gPrevRequestedTool = desc;
}

// Activate a tool and recursively activate the tool's dependencies. Tool
// dependencies are activated in-order.
static void ActivateTool(ToolDescription *desc) {
  if (desc->is_active) return;
  desc->is_active = true;

  for (auto i = 0ULL; i < desc->next_dependency_offset; ++i) {
    auto dep_id = gToolDependencies[desc->id][i];
    auto dep_desc = gRegisteredTools[dep_id];
    if (!dep_desc) {
      os::Log(os::LogDebug,
              "Error: Could not find tool `%s`, needed by tool `%s`.",
              gToolNames[dep_id], desc->name);
      continue;
    }
    ActivateTool(dep_desc);
  }

  gAllocationSize += GRANARY_ALIGN_FACTOR(gAllocationSize, desc->align);
  desc->allocation_offset = gAllocationSize;
  gAllocationSize += desc->size;
  gAllocationAlign = GRANARY_MAX(desc->align, gAllocationAlign);
  gActiveTools[gNextActiveTool++] = desc;
}

// Request that some tools be used for instrumentation.
static void RequestTools(void) {
  // Force register some tools that should get priority over all others.
  RequestTool(GRANARY_IF_KERNEL_ELSE("kernel", "user"));

#ifdef GRANARY_WITH_VALGRIND
  // Auto-registered so that `aligned_alloc` and `free` are always wrapped to
  // execute natively (and so are ideally instrumented by Valgrind to help
  // catch memory access bugs).
  RequestTool("valgrind");
#endif  // GRANARY_WITH_VALGRIND

  // Register tools specified at the command-line.
  if (FLAG_tools) {
    ForEachCommaSeparatedString<kMaxToolNameLength>(
        FLAG_tools,
        [=] (const char *tool_name) {
          RequestTool(tool_name);
        });
  }
}

}  // namespace

// Register a tool with Granary given its description.
void AddInstrumentationTool(
    ToolDescription *desc, const char *name,
    std::initializer_list<const char *> required_tools) {
  auto id = IdForName(name);
  desc->id = id;
  desc->next_dependency_offset = 1;
  desc->is_active = false;
  desc->name = &(gToolNames[id][0]);
  desc->allocation_offset = 0;
  gRegisteredTools[id] = desc;

  // Add the (ordered) dependencies.
  gToolDependencies[id][0] = id;
  for (auto dep_name : required_tools) {
    auto dep_id = IdForName(dep_name);
    gToolDependencies[id][desc->next_dependency_offset++] = dep_id;
  }
}

// Initialize the tool manager.
void InitToolManager(void) {
  RequestTools();
  ActivateTool(gPrevRequestedTool);

  auto size = GRANARY_ALIGN_TO(gAllocationSize, gAllocationAlign);
  auto allocation_offset = GRANARY_ALIGN_TO(sizeof(internal::SlabList),
                                            gAllocationAlign);
  auto remaining_size = internal::kNewAllocatorNumBytesPerSlab -
                        allocation_offset;
  auto max_num_allocs = (remaining_size - size + 1) / size;
  auto max_offset = allocation_offset + max_num_allocs * size;
  gToolAllocator.Construct(allocation_offset, max_offset, size, size);
}

// Exit the tool manager.
void ExitToolManager(void) {
  gToolAllocator.Destroy();
  gNextToolId = 0;
  memset(gRegisteredTools, 0, sizeof gRegisteredTools);
  memset(gActiveTools, 0, sizeof gActiveTools);
  gNextActiveTool = 0;
  memset(gToolDependencies, 0, sizeof gToolDependencies);
  gPrevRequestedTool = nullptr;
  gAllocationSize = 0;
  gAllocationAlign = 0;
}

// Initialize all tools. Tool initialization is typically where tools will
// register their specific their block meta-data, therefore it is important
// to initialize all tools before finalizing the meta-data manager.
void InitTools(InitReason reason) {
  for (auto desc : gActiveTools) {
    if (!desc) return;
    desc->init(reason);
  }
}

// Exit all tools. Tool `Exit` methods should restore any global state to
// their initial values.
void ExitTools(ExitReason reason) {
  for (auto desc : gActiveTools) {
    if (!desc) return;
    desc->exit(reason);
  }
}

// Allocates all tools, and returns a pointer to the first tool allocated.
InstrumentationTool *AllocateTools(void) {
  auto mem = gToolAllocator->Allocate();
  InstrumentationTool *tools(nullptr);
  auto prev_tool = &tools;
  for (auto desc : gActiveTools) {
    if (!desc) break;
    auto tool = reinterpret_cast<InstrumentationTool *>(
        reinterpret_cast<uintptr_t>(mem) + desc->allocation_offset);

    desc->construct(tool);

    *prev_tool = tool;
    prev_tool = &(tool->next);
  }
  return tools;
}

// Frees all tools, given a pointer to the first tool allocated.
void FreeTools(InstrumentationTool *tools) {
  for (auto desc : gActiveTools) {
    if (!desc) break;
    auto tool = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(tools) +
                                         desc->allocation_offset);
    desc->destruct(tool);
  }
  gToolAllocator->Free(tools);
}

}  // namespace granary
