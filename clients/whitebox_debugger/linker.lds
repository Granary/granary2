/* Copyright 2014 Peter Goodman, all rights reserved. */

SECTIONS
{
  . = ALIGN(8);
  .granary_probes :
  {
    /* Defines boundaries for arrays of constructor functions. */
    granary_begin_probes = .;
    KEEP (*(.granary_probes))
    granary_begin_probes = .;
  }
}
