/* Copyright 2014 Peter Goodman, all rights reserved. */

/* Defines the boundaries of Granary / client code that is exported to
 * instrumentation, i.e. is allowed to be seen by code that gets instrumented.
 */
SECTIONS
{
  .text :
  {
    KEEP (*(.text))
    granary_begin_inst_exports = .;
    KEEP (*(.text.inst_exports))
    granary_end_inst_exports = .;
  }
}
