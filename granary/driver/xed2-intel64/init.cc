/* Copyright 2014 Peter Goodman, all rights reserved. */

#include "granary/driver/xed2-intel64/xed.h"

namespace granary {
namespace driver {

// Decoder state that sets the mode to 64-bit.
xed_state_t XED_STATE;

// Initialize the driver (instruction encoder/decoder).
void Init(void) {
  xed_tables_init();
  xed_state_zero(&XED_STATE);
  xed_state_init(&XED_STATE, XED_MACHINE_MODE_LONG_64,
                 XED_ADDRESS_WIDTH_64b, XED_ADDRESS_WIDTH_64b);
}

}  // namespace driver
}  // namespace granary