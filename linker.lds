/* Copyright 2014 Peter Goodman, all rights reserved. */

SECTIONS
{
  . = ALIGN(16);
  .text :
  {
    *(.text)

    /* Defines the boundaries of Granary / client code that is exported to
     * instrumentation, i.e. is allowed to be seen by code that gets
     * instrumented.
     */
    granary_begin_inst_exports = .;
    KEEP(*(.text.inst_exports))
    granary_end_inst_exports = .;
  }
  
  . = ALIGN(16);
  .bss :
  {
    *(.bss.granary_unprotected) 
    
    /* Defines the boundaries of Granary's global variables. We use these
     * symbols to detect some forms of memory corruption by allocators.
     */
    granary_begin_protected_bss = .;
    *(.bss)
    *(.bss.*)
    granary_end_protected_bss = .;
  }
}
