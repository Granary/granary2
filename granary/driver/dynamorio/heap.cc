/* Copyright 2014 Peter Goodman, all rights reserved. */

#include <stdint.h>
#include <cstddef>

#include "granary/debug/breakpoint.h"
#include "granary/driver/dynamorio/decoder.h"
#include "granary/driver/dynamorio/instruction.h"

namespace granary {
namespace driver {

class DynamoRIOHeap {
 public:

  // Allocate memory for the DynamoRIO instruction encoder and decoder. This
  // takes pre-allocated memory from the in-flight instruction pointed to by
  // the `InstructionDecoder`.
  static void *Allocate(InstructionDecoder *decoder, size_t size,
                        dynamorio::which_heap_t acct) {

    while (dynamorio::ACCT_IR == acct) {
      // Allocate an instruction.
      if (sizeof(dynamorio::instr_t) == size) {
        if (decoder->allocated_instruction) {
          break;
        }
        decoder->allocated_instruction = true;
        return &(decoder->in_flight_instruction->instruction);

      // Allocate the raw bytes to hold an instruction.
      } else if (DecodedInstruction::MAX_NUM_RAW_BYTES == size) {
        if (decoder->allocated_raw_bytes) {
          break;
        }
        decoder->allocated_raw_bytes = true;
        return &(decoder->in_flight_instruction->raw_bytes[0]);

      // Allocate the operands of an instruction.
      } else if (0 == (size % sizeof(dynamorio::opnd_t))) {
        const unsigned long num_operands(size / sizeof(dynamorio::opnd_t));
        if (DecodedInstruction::MAX_NUM_OPERANDS <
            (decoder->num_allocated_operands + num_operands)) {
          break;
        }

        void *ret(&(decoder->in_flight_instruction->
            operands[decoder->num_allocated_operands]));
        decoder->num_allocated_operands += num_operands;
        return ret;
      }

      break;
    }
    granary_break_unreachable();
    return nullptr;
  }
};

}  // namespace driver
}  // namespace granary

extern "C" {

// C interface to the allocator for the DynamoRIO heap.
void *dynamorio_heap_alloc(granary::driver::InstructionDecoder *decoder,
                           size_t size,
                           dynamorio::which_heap_t acct) {
  return granary::driver::DynamoRIOHeap::Allocate(decoder, size, acct);
}

// C interface to the deallocator for the DynamoRIO heap.
void dynamorio_heap_free(granary::driver::InstructionDecoder *decoder,
                         void *mem,
                         size_t size,
                         dynamorio::which_heap_t acct) {
  GRANARY_UNUSED(decoder);
  GRANARY_UNUSED(mem);
  GRANARY_UNUSED(size);
  GRANARY_UNUSED(acct);
}

}


