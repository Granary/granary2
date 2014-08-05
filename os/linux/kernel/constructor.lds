/* Copyright 2014 Peter Goodman, all rights reserved. */

/* Defines boundaries for arrays of constructor functions. */
SECTIONS
{
  .init_array :
  {
    granary_begin_init_array = .;
    *(.init_array)
    granary_end_init_array = .;
  }
}
