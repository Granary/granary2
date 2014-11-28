
#include "clients/user/syscall.h"

// Amount by which addresses should be shifted.
static int gNativeGranularity = 4096;
static uint64_t gShiftAmountLong = 0UL;
static uint8_t gShiftAmount = 0;

// Amount by which shifted addresses should be multiplied.
static int gScaleAmount = 1;

// Size (in bytes) of the shadow memory.
static auto gShadowMemSize = 0UL;

// Base and limit of shadow memory.
static void *gBeginShadowMem = nullptr;
static void *gEndShadowMem = nullptr;




// Tells us if we came across a `clone` system call.
static __thread bool tIsClone = false;

#ifdef GRANARY_WHERE_user
// Find `clone` system calls, which are used for spawning threads.
static void FindClone(void *, SystemCallContext context) {
  tIsClone = __NR_clone == context.Number();
}

// After a `clone` system call, set the `GS` segment base to point to shadow
// memory.
//
// There's a bit of duplication here in that we'll set the `GS` base on both
// the new thread and the old thread, but that doesn't matter.
static void SetupShadowSegment(void *, SystemCallContext) {
  if (!tIsClone) return;
  GRANARY_IF_DEBUG( auto ret = ) arch_prctl(ARCH_SET_GS, gBeginShadowMem);
  GRANARY_ASSERT(!ret);
  tIsClone = false;
}
#endif  // GRANARY_WHERE_user

// Allocates shadow memory.
static char *AllocateShadowMemory(void) {
#ifdef GRANARY_WHERE_user
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wold-style-cast"
  auto ret = mmap(nullptr, gShadowMemSize,
                           PROT_READ | PROT_WRITE,  // Fault on first access.
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                           -1, 0);

  if (MAP_FAILED == ret) {
    os::Log(os::LogDebug, "Failed to map shadow memory. Exiting.\n");
    exit(EXIT_FAILURE);
  }
  return reinterpret_cast<char *>(ret);
# pragma clang diagnostic pop
#else
  return nullptr;
#endif  // GRANARY_WHERE_user
}



GRANARY_ASSERT(0 < gScaleAmount);
GRANARY_ASSERT(0 < gNativeGranularity);

gShiftAmountLong = static_cast<uint64_t>(__builtin_ctzl(
    static_cast<uint64_t>(gNativeGranularity)));

gShiftAmount = static_cast<uint8_t>(gShiftAmountLong);
gShadowMemSize = kAddressSpaceSize >> gShiftAmountLong;
gShadowMemSize = GRANARY_ALIGN_TO(gShadowMemSize, arch::PAGE_SIZE_BYTES);
gBeginShadowMem = AllocateShadowMemory();
gEndShadowMem = gBeginShadowMem + gShadowMemSize;

#ifdef GRANARY_WHERE_user
// Make it so that the `GS` segment points to our shadow memory.
GRANARY_IF_DEBUG( auto ret = ) arch_prctl(ARCH_SET_GS, gBeginShadowMem);
GRANARY_ASSERT(!ret);

// Interpose on clone system calls so that we can setup the shadow memory.
AddSystemCallEntryFunction(FindClone);
AddSystemCallExitFunction(SetupShadowSegment);
#endif  // GRANARY_WHERE_user
