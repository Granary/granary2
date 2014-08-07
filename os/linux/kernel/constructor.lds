/* Copyright 2014 Peter Goodman, all rights reserved. */

/* Defines boundaries for arrays of constructor functions. */
SECTIONS
{
  . = ALIGN(8);
  .init_array :
  {
    granary_begin_init_array = .;
    KEEP (*(.init_array.*))
    KEEP (*(.init_array))
    granary_end_init_array = .;
  }
}
