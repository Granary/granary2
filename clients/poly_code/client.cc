/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "clients/wrap_func/client.h"
#include "clients/watchpoints/client.h"
#include "clients/watchpoints/client.h"

#include "generated/clients/poly_code/offsets.h"

GRANARY_USING_NAMESPACE granary;

GRANARY_DEFINE_bool(record_block_types, true,
    "Should we record the specific types accessed by each basic block? If not, "
    "then all that will be recorded is that a particular block accessed data "
    "of any type. The default value is `yes`.",

    "poly_code");

// Track the set of types accessed by each basic block.
class TypeMetaData : public MutableMetaData<TypeMetaData> {
 public:
  TypeMetaData(void)
      : type_ids(),
        type_ids_lock(),
        accesses_typed_data(false) {}

  TypeMetaData(const TypeMetaData &that)
      : accesses_typed_data(that.accesses_typed_data) {
    ReadLockedRegion locker(&(that.type_ids_lock));
    type_ids = that.type_ids;
  }

  // Set of `type_id + 1` for this block. We use `type_id + 1` because the
  // default initializer of `uint16_t` is `0`.
  TinySet<uint16_t, 4> type_ids;
  mutable ReaderWriterLock type_ids_lock;

  // Does this block access typed data? This is only used if
  // `--record_block_types` is `false`.
  bool accesses_typed_data;
};

namespace {

// If we care about reporting specific types, then use a different type id per
// allocation. Otherwise, use the same type id for all allocations.
static uintptr_t GetTypeId(AppPC ret_address, size_t size) {
  if (FLAG_record_block_types) return TypeIdFor(ret_address, size);
  return ~0UL;
}

#define GET_ALLOCATOR(name) \
  auto name = WRAPPED_FUNCTION; \
  auto ret_address = NATIVE_RETURN_ADDRESS

// Return a tainted allocated address.
#define RETURN_TAINTED_ADDRESS \
    if (addr) { \
      auto type_id = GetTypeId(ret_address, size); \
      addr = TaintAddress(addr, type_id); \
    } \
    return addr \

#ifdef GRANARY_WHERE_user

// Make a wrapper for an allocator.
#define ALLOC_WRAPPER(lib, name) \
    WRAP_NATIVE_FUNCTION(lib, name, (void *), (size_t size)) { \
      GET_ALLOCATOR(name); \
      auto addr = name(size); \
      RETURN_TAINTED_ADDRESS; \
    }

ALLOC_WRAPPER(libc, malloc)
ALLOC_WRAPPER(libc, valloc)
ALLOC_WRAPPER(libc, pvalloc)
ALLOC_WRAPPER(libstdcxx, _Znwm)
ALLOC_WRAPPER(libstdcxx, _Znam)
ALLOC_WRAPPER(libcxx, _Znwm)
ALLOC_WRAPPER(libcxx, _Znam)

// Make a wrapper for an allocator.
WRAP_NATIVE_FUNCTION(libc, aligned_alloc, (void *), (size_t align,
                                                     size_t size)) {
  GET_ALLOCATOR(aligned_alloc);
  auto addr = aligned_alloc(align, size);
  RETURN_TAINTED_ADDRESS;
}

// Make a wrapper for an allocator.
WRAP_NATIVE_FUNCTION(libc, memalign, (void *), (size_t align, size_t size)) {
  GET_ALLOCATOR(memalign);
  auto addr = memalign(align, size);
  RETURN_TAINTED_ADDRESS;
}

// Make a wrapper for an allocator.
WRAP_NATIVE_FUNCTION(libc, posix_memalign, (int), (void **addr_ptr,
                                                   size_t align, size_t size)) {
  GET_ALLOCATOR(posix_memalign);
  auto ret = posix_memalign(addr_ptr, align, size);
  if (!ret) {
    auto type_id = TypeIdFor(ret_address, size);
    *addr_ptr = TaintAddress(*addr_ptr, type_id);
  }
  return ret;
}

// Make a wrapper for an allocator.
WRAP_NATIVE_FUNCTION(libc, calloc, (void *), (size_t count, size_t size)) {
  GET_ALLOCATOR(calloc);
  auto addr = calloc(count, size);
  RETURN_TAINTED_ADDRESS;
}

// Make a wrapper for a re-allocator.
WRAP_NATIVE_FUNCTION(libc, realloc, (void *), (void *ptr, size_t new_size)) {
  auto realloc = WRAPPED_FUNCTION;
  if (!IsTaintedAddress(ptr)) {
    return realloc(ptr, new_size);
  } else {
    auto type_id = ExtractTaint(ptr);
    ptr = realloc(UntaintAddress(ptr), new_size);
    return TaintAddress(ptr, type_id);
  }
}

// Deallocate some memory.
#define FREE_WRAPPER(lib, name) \
    WRAP_NATIVE_FUNCTION(lib, name, (void), (void *ptr)) { \
      auto name = WRAPPED_FUNCTION; \
      name(UntaintAddress(ptr)); \
    }

FREE_WRAPPER(libc, free)
FREE_WRAPPER(libstdcxx, _ZdlPv)
FREE_WRAPPER(libstdcxx, _ZdaPv)
FREE_WRAPPER(libcxx, _ZdlPv)
FREE_WRAPPER(libcxx, _ZdaPv)

#endif  // GRANARY_WHERE_user

}  // namespace

// Simple tool for static and dynamic basic block counting.
class PolyCode : public InstrumentationTool {
 public:
  virtual ~PolyCode(void) = default;

  static void Init(InitReason reason) {
    if (kInitProgram == reason || kInitAttach == reason) {
#ifdef GRANARY_WHERE_user
      // Wrap libc.
      AddFunctionWrapper(&WRAP_FUNC_libc_malloc);
      AddFunctionWrapper(&WRAP_FUNC_libc_valloc);
      AddFunctionWrapper(&WRAP_FUNC_libc_pvalloc);
      AddFunctionWrapper(&WRAP_FUNC_libc_aligned_alloc);
      AddFunctionWrapper(&WRAP_FUNC_libc_memalign);
      AddFunctionWrapper(&WRAP_FUNC_libc_posix_memalign);
      AddFunctionWrapper(&WRAP_FUNC_libc_calloc);
      AddFunctionWrapper(&WRAP_FUNC_libc_realloc);
      AddFunctionWrapper(&WRAP_FUNC_libc_free);

      // Wrap GNU's C++ standard library.
      AddFunctionWrapper(&WRAP_FUNC_libstdcxx__Znwm);
      AddFunctionWrapper(&WRAP_FUNC_libstdcxx__Znam);
      AddFunctionWrapper(&WRAP_FUNC_libstdcxx__ZdlPv);
      AddFunctionWrapper(&WRAP_FUNC_libstdcxx__ZdaPv);

      // Wrap clang's C++ standard library.
      AddFunctionWrapper(&WRAP_FUNC_libcxx__Znwm);
      AddFunctionWrapper(&WRAP_FUNC_libcxx__Znam);
      AddFunctionWrapper(&WRAP_FUNC_libcxx__ZdlPv);
      AddFunctionWrapper(&WRAP_FUNC_libcxx__ZdaPv);
#endif  // GRANARY_WHERE_user

      AddWatchpointInstrumenter(CallTaintBlock);
      AddMetaData<TypeMetaData>();
    }
  }

  static void Exit(ExitReason reason) {
    if (kExitProgram == reason || kExitDetach == reason) {
      if (FLAG_record_block_types) ForEachType(LogTypeInfo);
      ForEachMetaData(LogMetaInfo);
    }
  }

 private:

  // Taint a basic block with some type id.
  static void TaintBlock(TypeMetaData *meta, void *address) {
    if (0x7fUL != (reinterpret_cast<uintptr_t>(meta) >> 40)) {
      granary_curiosity();
    }
    auto type_id = static_cast<uint16_t>(ExtractTaint(address) + 1);
    {
      ReadLockedRegion locker(&(meta->type_ids_lock));
      if (meta->type_ids.Contains(type_id)) return;
    }
    {
      WriteLockedRegion locker(&(meta->type_ids_lock));
      meta->type_ids.Add(type_id);
    }
  }

  // Taints block meta-data when some watchpoint is triggered.
  static void CallTaintBlock(const WatchedMemoryOperand &op) {
    auto meta = GetMetaData<TypeMetaData>(op.block);
    if (!meta) return;
    if (FLAG_record_block_types) {
      op.instr->InsertBefore(lir::InlineFunctionCall(op.block,
          TaintBlock, meta, op.watched_reg_op));
    } else {
      MemoryOperand is_typed(&(meta->accesses_typed_data));
      lir::InlineAssembly asm_(is_typed);
      asm_.InlineBefore(op.instr, "OR m8 %0, i8 1;");
    }
  }

  // Log info about what block (i.e. return address into a block) defines
  // a type.
  static void LogTypeInfo(uint64_t type_id, AppPC ret_address,
                          size_t size_order) {
    auto offset = os::ModuleOffsetOfPC(ret_address);
    if (offset.module) {
      os::Log("T %u %lu B %s %lx\n", type_id, size_order,
              offset.module->Name(), offset.offset);
    } else {
      os::Log("T %u %lu A %p\n", type_id, size_order, ret_address);
    }
  }

  // Log the types of data accessed by each block.
  static void LogMetaInfo(const BlockMetaData *meta, IndexedStatus) {
    auto app_meta = MetaDataCast<const AppMetaData *>(meta);
    auto type_meta = MetaDataCast<const TypeMetaData *>(meta);
    auto offset = os::ModuleOffsetOfPC(app_meta->start_pc);
    os::Log("B %s %lx", offset.module->Name(), offset.offset);
    auto sep = " Ts ";
    if (FLAG_record_block_types) {
      for (auto type_id : type_meta->type_ids) {
        os::Log("%s%lu", sep, static_cast<uint64_t>(type_id - 1));
        sep = ",";
      }
    } else if (type_meta->accesses_typed_data) {
      os::Log("%s*", sep);
    }
    os::Log("\n");
  }
};

// Initialize the `poly_code` tool.
GRANARY_ON_CLIENT_INIT() {
  AddInstrumentationTool<PolyCode>("poly_code", {"wrap_func", "watchpoints"});
}
