/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef CLIENTS_WATCHPOINTS_CLIENT_H_
#define CLIENTS_WATCHPOINTS_CLIENT_H_

#include <granary.h>

// Forward declaration.
class Watchpoints;

class WatchedMemoryOperand {
 public:
  granary::DecodedBlock * const block;

  // Instruction that contains the memory operand `mem_op`.
  granary::NativeInstruction * const instr;

  // Memory operand that de-references a potentially watched address.
  const granary::MemoryOperand &mem_op;

  // Register operand, where the register will contain the unwatched address.
  const granary::RegisterOperand &unwatched_reg_op;

  // Register operand, where the register will contain the watched address.
  const granary::RegisterOperand &watched_reg_op;

 protected:
  friend class Watchpoints;

  inline WatchedMemoryOperand(granary::DecodedBlock *block_,
                        granary::NativeInstruction *instr_,
                        const granary::MemoryOperand &mem_op_,
                        const granary::RegisterOperand &unwatched_reg_op_,
                        const granary::RegisterOperand &watched_reg_op_)
      : block(block_),
        instr(instr_),
        mem_op(mem_op_),
        unwatched_reg_op(unwatched_reg_op_),
        watched_reg_op(watched_reg_op_) {}

 private:
  GRANARY_DISALLOW_COPY_AND_ASSIGN(WatchedMemoryOperand);
};

enum {
  kMaxWatchpointTypeId = std::numeric_limits<uint16_t>::max() >> 1U
};

// Returns the type id for some `(return address, allocation size)` pair.
uint64_t TypeIdFor(uintptr_t ret_address, size_t num_bytes);

// Returns the type id for some `(return address, allocation size)` pair.
inline static uint64_t TypeIdFor(granary::AppPC ret_address, size_t num_bytes) {
  return TypeIdFor(reinterpret_cast<uintptr_t>(ret_address), num_bytes);
}

// Apply a function to every type.
void ForEachType(std::function<void(uint64_t type_id,
                                    granary::AppPC ret_address,
                                    size_t size_order)> func);

// Registers a function that can hook into the watchpoints system to instrument
// code.
void AddWatchpointInstrumenter(void (*func)(const WatchedMemoryOperand &));

// Taints an address `addr` using the low 15 bits of the taint index `index`.
uintptr_t TaintAddress(uintptr_t addr, uintptr_t index);

// Untaints an address `addr`.
uintptr_t UntaintAddress(uintptr_t addr);

// Returns true if an address is tainted.
bool IsTaintedAddress(uintptr_t addr);

// Returns the taint for an address.
uint16_t ExtractTaint(uintptr_t addr);

// Taints an pointer `ptr` using the low 15 bits of the taint index `index`.
template <typename T, typename I>
inline static T *TaintAddress(T *ptr, I taint) {
  return reinterpret_cast<T *>(TaintAddress(
      reinterpret_cast<uintptr_t>(ptr), static_cast<uintptr_t>(taint)));
}

// Untaints a pointer `ptr`.
template <typename T>
inline static T *UntaintAddress(T *ptr) {
  return reinterpret_cast<T *>(UntaintAddress(
      reinterpret_cast<uintptr_t>(ptr)));
}

// Returns true if a pointer `ptr` is tainted.
template <typename T>
inline static bool IsTaintedAddress(T *ptr) {
  return IsTaintedAddress(reinterpret_cast<uintptr_t>(ptr));
}

// Returns the taint for an address. This assumes the address is tainted.
template <typename T>
inline static uint16_t ExtractTaint(T *ptr) {
  return ExtractTaint(reinterpret_cast<uintptr_t>(ptr));
}

#endif  // CLIENTS_WATCHPOINTS_CLIENT_H_
