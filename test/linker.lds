/* Copyright 2014 Peter Goodman, all rights reserved. */

SECTIONS
{
  .text :
  {
    KEEP (*(.text))
    
    /* Defines the boundaries of Granary test instrumentation exports. */
    granary_begin_test_exports = .;
    KEEP (*(.text.inst_exports))
    granary_end_test_exports = .;
    
    /* Defines the boundaries of Granary test code. */
    granary_begin_test_cases = .;
    KEEP (*(.text.test_cases))
    granary_end_test_cases = .;
  }
}
