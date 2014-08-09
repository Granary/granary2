set logging off
set breakpoint pending on
set print demangle on
set print asm-demangle on
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
b granary_unreachable
b granary_curiosity

# Kernel breakpoints
if !$in_user_space
  b panic
  b show_fault_oops
  b invalid_op
  b do_invalid_op
  b do_general_protection
  b __schedule_bug
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

b __stack_chk_fail

# Alias for `attach` to deal with mistyping the command.
define attacj
  attach $arg0
end
define a
  attach $arg0
end

# Sort of like `reset` from the terminal.
define reset
  shell clear
end

# Print $arg1 instructions starting at address $arg0.
define pi
  set $__rip = $arg0
  set $__ni = $arg1
  python None ; \
    rip = str(gdb.parse_and_eval("$__rip")).lower() ; \
    ni = str(gdb.parse_and_eval("$__ni")).lower() ; \
    gdb.execute( \
      "x/%si %s\n" % (ni, rip), \
      from_tty=True, to_string=False) ;
end

# Print the $arg0th most recent entry in the trace log, where 0 is the
# most recent log entry.
#
# An optional second parameter can be specified, which is the number of
# instructions to disassemble from the traced block.
define pt
  set $__i = granary_block_log_index + GRANARY_BLOCK_LOG_LENGTH - $arg0
  set $__i = ($__i) % GRANARY_BLOCK_LOG_LENGTH
  set $__r = &(granary_block_log[$__i])

  # Figure out the block start pc, $__brip, and the first non-trace logger
  # instruction of the block, $__rip.
  set $__rip = $__r->rip
  set $__brip = $__r->rip - 5
  if $in_user_space
    # Adjust the instruction pointer for the size of `lea rsp, [rsp + 128]`
    set $__rip = $__rip + 8

    # Adjust the block start pc for `lea rsp, [rsp - 128]`, whihc is 5 bytes
    # (not 8).
    set $__brip = $__brip - 5
  end

  # Figure out if the tracer injected an indirect or direct call, where a
  # direct call is 5 bytes long with opcode 0xe8 and an indirect call is 6 bytes
  # long with opcode 0xff.
  if 0xe8 != *((unsigned char *) ($__r->rip - 5))
    set $__brip = $__brip - 1
  end

  # Print the regs state.
  printf "Trace log entry %d\n", $arg0
  printf "   r15 = %#18lx   r14 = %#18lx\n", $__r->r15, $__r->r14
  printf "   r13 = %#18lx   r12 = %#18lx\n", $__r->r13, $__r->r12
  printf "   r11 = %#18lx   r10 = %#18lx\n", $__r->r11, $__r->r10
  printf "   r9  = %#18lx   r9  = %#18lx\n", $__r->r9,  $__r->r8
  printf "   rdi = %#18lx   rsi = %#18lx\n", $__r->rdi, $__r->rsi
  printf "   rbp = %#18lx   rbx = %#18lx\n", $__r->rbp, $__r->rbx
  printf "   rdx = %#18lx   rcx = %#18lx\n", $__r->rdx, $__r->rcx
  printf "   rax = %#18lx   rip = %#18lx\n", $__r->rax, $__brip
  printf "   flags ="

  # Print the flags state.
  if $__r->rflags & (1 << 0)
    printf " CF"
  end
  if $__r->rflags & (1 << 2)
    printf " PF"
  end
  if $__r->rflags & (1 << 4)
    printf " AF"
  end
  if $__r->rflags & (1 << 6)
    printf " ZF"
  end
  if $__r->rflags & (1 << 7)
    printf " SF"
  end
  if $__r->rflags & (1 << 8)
    printf " TF"
  end
  if $__r->rflags & (1 << 9)
    printf " IF"
  end
  if $__r->rflags & (1 << 10)
    printf " DF"
  end
  if $__r->rflags & (1 << 11)
    printf " OF"
  end
  if $__r->rflags & (1 << 13)
    printf " NT"
  end
  if $__r->rflags & (1 << 16)
    printf " RF"
  end
  printf "\n"

  if $argc == 2
    printf "\nFirst %d instructions:\n", $arg1
    pi $__rip $arg1
  end
  
  dont-repeat
end
