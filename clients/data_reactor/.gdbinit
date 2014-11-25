# Copyright 2014 Peter Goodman, all rights reserved.

set $__wp = 1

define sample
  set language c++

  # Make it so that if a watchpoint is hit in one thread, other threads
  # continue to execute.
  set non-stop on

  # Attach to the process.
  attach $arg0

  b gdb_data_reactor_change_sample_address 
  commands

    # Delete the last added watchpoint.
    if $__wp > 1
      delete $__wp
      set $__wp = $__wp + 1
    end

    # Nice helpful message :D
    printf "--------------------------------------------------\n"
    printf "Sampling shadow address %lx\n", $rdi
    printf "--------------------------------------------------\n"

    # Add a new hardware watchpoint to a specific address.
    python gdb.execute("awatch *(char*)%s" % gdb.parse_and_eval("$rdi"))

    # Associate commands with the just-added to run a backtrace then continue
    # execution.
    commands
      bt
      c
    end
  end

  # Continue the program after attaching.
  c
end
