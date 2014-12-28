# Copyright 2014 Peter Goodman, all rights reserved.

set $__wp = 1

# sample
#
# Run the DataReactor sampler on the process with PID `$arg0`. This will dump
# the recorded traces to `/tmp/$arg0.trace`.
define sample
  set language c++
  set backtrace limit 20
  set logging file /tmp/$arg0.pid
  set logging on
  set logging redirect on

  # Make it so that if a watchpoint is hit in one thread, other threads
  # continue to execute.
  set non-stop on

  # Attach to the process.
  attach $arg0

  b granary_gdb_event1 
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

    c
  end

  # Continue the program after attaching.
  c
end


# end-sample
#
# End DataReactor sampling. 
define end-sample
  set logging off
  set logging redirect off
  clear
  q
end
