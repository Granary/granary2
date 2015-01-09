/* Copyright 2014 Peter Goodman, all rights reserved. */

SECTIONS
{
  
  .text :
  {
    granary_begin_text = .;
    KEEP (*(.text))
        
    /* Defines the boundaries of Granary / client code that is exported to
     * instrumentation, i.e. is allowed to be seen by code that gets
     * instrumented.
     */
    granary_begin_inst_exports = .;
    KEEP (*(.text.inst_exports))
    granary_end_inst_exports = .;
    
    granary_end_text = .;
  }
  
  /* Used for code cache allocations */
  . = ALIGN(16);
  .writable_text :
  {
    granary_begin_writable_text = .;
    KEEP (*(.writable_text))
  }
  
  . = ALIGN(16);
  .bss :
  {
    KEEP(*(.bss.granary_unprotected))
    
    /* Defines the boundaries of Granary's global variables. We use these
     * symbols to detect some forms of memory corruption by allocators.
     */
    granary_begin_protected_bss = .;
    KEEP (*(.bss))
    KEEP (*(.bss.*))
    granary_end_protected_bss = .;
  }
  
  . = ALIGN(8);
  .granary_init_array :
  {
    /* Defines boundaries for arrays of constructor functions. */
    granary_begin_init_array = .;
    KEEP (*(.preinit_array))
    KEEP (*(SORT(.ctors.*)))
    KEEP (*(.ctors))
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array))
    granary_end_init_array = .;
  }
  
  . = ALIGN(8);
  .granary_fini_array :
  {
    /* Defines boundaries for arrays of destructor functions. */
    granary_begin_fini_array = .;
    KEEP (*(SORT(.dtors.*)))
    KEEP (*(.dtors))
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array))
    granary_end_fini_array = .;
  }
}
