set logging off
set breakpoint pending on
set print demangle on
set print asm-demangle off
set print object on
set print static-members on
set disassembly-flavor intel
set language c++

# set-user-detect
#
# Uses Python support to set the variable `$in_user_space`
# to `0` or `1` depending on whether we are instrumenting in
# user space or kernel space, respectively.
define set-user-detect
  python None ; \
    gdb.execute( \
      "set $in_user_space = %d" % int(None is not gdb.current_progspace().filename), \
      from_tty=True, to_string=True)
end

# Detect if we're in user space and set `$in_user_space`
# appropriately.
set-user-detect

# Kernel setup
if !$in_user_space
  
  # Symolic link made by `scripts/make_vmlinux_link.sh`.
  file vmlinux

  target remote : 9999

  # File with loaded Granary symbols, made by `scripts/vmload.py`.
  source /tmp/granary.syms
end

# Common Granary breakpoints.
catch throw
b granary_break_on_fault
b granary_break_on_unreachable_code
b granary_break_on_encode
b granary_break_on_decode

# Kernel breakpoints
if !$in_user_space
  b panic
  b show_fault_oops
  b invalid_op
  b do_invalid_op
  b do_general_protection
  b __schedule_bug
  b __stack_chk_fail
  b __die
  b do_spurious_interrupt_bug
  b report_bug
  b dump_stack
  b show_stack
  b show_trace
  b show_trace_log_lvl
else
  b __assert_fail
end