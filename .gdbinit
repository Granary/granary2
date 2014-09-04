set logging off
set breakpoint pending on
set print demangle on
set print asm-demangle on
set print object on
set print static-members on
set disassembly-flavor intel
set language c++

# Make sure that the `re` module is available for use for executing Python code.
python import re ;

# set-user-detect
#
# Uses Python support to set the variable `$in_user_space`
# to `0` or `1` depending on whether we are instrumenting in
# user space or kernel space, respectively.
define set-user-detect
  python None ; \
    gdb.execute( \
      "set $in_user_space = %d" % \
        int(None is not gdb.current_progspace().filename), \
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

# User setup.
else
  set record stop-at-limit off
end

# Generic breakpoints.
b granary_unreachable
b granary_curiosity
b __stack_chk_fail

# Kernel space breakpoints
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

# User space breakpoints.
else
  b __assert_fail
end


# attacj
#
# Attach to process with PID `$arg0`.
#
# Note: This is an intentional misspelling of `attach`, as it is easy to
#       accidentally type a `j` instead of an `h`.
define attacj
  attach $arg0
end


# a
#
# Attach to process with PID `$arg0`.
define a
  attach $arg0
end


# reset
#
# Short form for `shell clear`.
define reset
  shell clear
end


# print-instructions
#
# Print `$arg1` instructions starting at address `$arg0`.
define print-instructions
  set $__rip = $arg0
  set $__ni = $arg1
  python None ; \
    rip = str(gdb.parse_and_eval("$__rip")).lower() ; \
    ni = str(gdb.parse_and_eval("$__ni")).lower() ; \
    gdb.execute( \
      "x/%si %s\n" % (ni, rip), \
      from_tty=True, to_string=False) ;
end


# print-exec-entry-flag
#
# Prints `$arg2` (a string representation of a flag) if bit `$arg1` is set in
# `$arg0` (a copy of the flags register state).
define print-exec-entry-flag
  set $__flags = $arg0
  set $__bit = $arg1
  set $__str = $arg2
  if $__flags & (1 << $__bit)
    # For compatibility with kernel debugging, we need to invoke python to print
    # `$__str`, otherwise GDB will report that we need `malloc` to complete the
    # operation.
    python None ; \
      fl = str(gdb.parse_and_eval("$__str")).upper() ; \
      gdb.write(" %s" % fl[-3:-1]) ;
  end
end


# print-exec-entry-impl
#
# Prints the `$arg0`th most recent execution trace entry, where `0` is the most
# recent entry.
#
# If a second parameter is specified, then `$arg1` instructions starting from
# the start of the block will be printed.
#
# Note: The printed instructions will exclude the call to the execution tracer
#       itself; however, the `RIP` reported by the entry will be faithful to
#       the true beginning of the block in the code cache.
define print-exec-entry-impl
  set $__r = $arg1

  # Adjust the instruction pointer for the size of the `CALL_NEAR_REBRd`.
  set $__rip = $__r->rip + 5
  
  # Adjust the instruction pointer for the size of the stack-shifting `LEA`s.
  if $in_user_space
    set $__rip = $__rip + 13
  end

  # Print the regs state.
  printf "Trace log entry %u in thread %ld\n", $arg0, $__r->thread
  printf "   r15 = %#18lx   r14 = %#18lx\n", $__r->r15, $__r->r14
  printf "   r13 = %#18lx   r12 = %#18lx\n", $__r->r13, $__r->r12
  printf "   r11 = %#18lx   r10 = %#18lx\n", $__r->r11, $__r->r10
  printf "   r9  = %#18lx   r8  = %#18lx\n", $__r->r9,  $__r->r8
  printf "   rdi = %#18lx   rsi = %#18lx\n", $__r->rdi, $__r->rsi
  printf "   rbp = %#18lx   rbx = %#18lx\n", $__r->rbp, $__r->rbx
  printf "   rdx = %#18lx   rcx = %#18lx\n", $__r->rdx, $__r->rcx
  printf "   rax = %#18lx   rip = %#18lx\n", $__r->rax, $__r->rip
  printf "   flags ="

  # Print the flags state.
  print-exec-entry-flag $__r->rflags 0 "CF"
  print-exec-entry-flag $__r->rflags 2 "PF"
  print-exec-entry-flag $__r->rflags 4 "AF"
  print-exec-entry-flag $__r->rflags 6 "ZF"
  print-exec-entry-flag $__r->rflags 7 "SF"
  print-exec-entry-flag $__r->rflags 8 "TF"
  print-exec-entry-flag $__r->rflags 9 "IF"
  print-exec-entry-flag $__r->rflags 10 "DF"
  print-exec-entry-flag $__r->rflags 11 "OF"
  print-exec-entry-flag $__r->rflags 13 "NT"
  print-exec-entry-flag $__r->rflags 16 "RF"
  printf "\n"

  if $argc == 3
    printf "\nFirst %d instructions:\n", $arg2
    print-instructions $__rip $arg2
  end
  
  dont-repeat
end


# print-exec-entry
#
# Prints the `$arg0`th most recent execution trace entry, where `0` is the most
# recent entry.
#
# If a second parameter is specified, then `$arg1` instructions starting from
# the start of the block will be printed.
#
# Note: The printed instructions will exclude the call to the execution tracer
#       itself; however, the `RIP` reported by the entry will be faithful to
#       the true beginning of the block in the code cache.
#
# Note: The program must be run with `--debug_trace_exec=yes`.
define print-exec-entry
  set $__i = granary_block_log_index + GRANARY_BLOCK_LOG_LENGTH - $arg0 - 1
  set $__r = &(granary_block_log[$__i % GRANARY_BLOCK_LOG_LENGTH])

  if 2 == $argc
    print-exec-entry-impl $arg0 $__r $arg1
  else
    print-exec-entry-impl $arg0 $__r
  end

  dont-repeat
end


# find-exec-entry
#
# Finds and prints the most recent execution trace entry, where the `rip` of the
# entry equals `$arg0`.
define find-exec-entry
  set language c++
  set $__i = granary_block_log_index - 1
  set $__min_i = $__i - GRANARY_BLOCK_LOG_LENGTH
  set $__offset = 0
  set $__found = 0

  while $__i >= 0 && $__i >= $__min_i
    set $__r = &(granary_block_log[$__i % GRANARY_BLOCK_LOG_LENGTH])
    if $__r->rip == $arg0
      set $__found = 1
      set $__i = 0
    end
    set $__i = $__i - 1
    set $__offset = $__offset + 1
  end
  if $__found
    print-exec-entry $__offset
  end
  dont-repeat
end


# print-block-meta
#
# Interpret `$arg0` as a pointer to a `BlockMetaData` structure, and dump its
# contents in a readable form.
define print-block-meta
  set language c++
  set $__m = (granary::BlockMetaData *) $arg0
  set $__man = *((granary::MetaDataManager **) $arg0)
  set $__offsets = &($__man->offsets[0])
  set $__descs = &($__man->descriptions[0])
  set $__i = 0
  while $__descs[$__i]
    set $__offset = $__offsets[$__i]
    set $__desc = $__descs[$__i]
    set $__bytes = ((char *) $__m) + $__offset
    python None ; \
      dstr = gdb.execute("p *$__desc\n", from_tty=True, to_string=True) ; \
      m = re.search(r"granary::Construct<([^>]+)>", dstr) ; \
      cls = m.group(1) ; \
      ms = gdb.execute("p *((%s *)$__bytes)" % cls, \
        from_tty=True, to_string=True) ; \
      ms = re.sub(r"^\$[0-9]+ = \{.*, <No data fields>},", "{", ms) ; \
      gdb.write("   %s = %s" % (cls, ms)) ;
    set $__i = $__i + 1
  end
  dont-repeat
end


# print-meta-entry
#
# Prints the `$arg0`th most recently translated basic block's meta-data from
# the meta-data trace, where `0` is the most recent entry.
#
# Note: The program must be run with `--debug_trace_meta=yes`.
define print-meta-entry
  set language c++
  set $__i = granary_meta_log_index + GRANARY_META_LOG_LENGTH - $arg0
  set $__i = ($__i - 1) % GRANARY_META_LOG_LENGTH
  set $__m = (granary::BlockMetaData *) granary_meta_log[$__i].meta
  set $__g = (unsigned long) granary_meta_log[$__i].group
  printf "Meta-data %p in group %lu:\n", $__m, $__g
  print-block-meta $__m
  dont-repeat
end


# find-meta-entry
#
# Finds and prints the block meta-data whose `AppMetaData::start_pc == $arg0`
# or `CacheMetaData::start_pc == $arg0`, assuming that meta-data is still
# located in the meta-data trace.
define find-meta-entry
  set language c++
  set $__pc = (granary::CachePC) $arg0
  set $__i = 0
  set $__g = 0
  set $__m = (granary::BlockMetaData *) 0

  while $__i < GRANARY_META_LOG_LENGTH
    set $__sm = (char *) granary_meta_log[$__i].meta
    if $__sm
      set $__fpc_app = *((granary::AppPC *) &($__sm[8]))
      set $__fpc_cache = *((granary::CachePC *) &($__sm[16]))
      if $__fpc_app == $__pc || $__fpc_cache == $__pc
        set $__g = granary_meta_log[$__i].group
        set $__m = (granary::BlockMetaData *) $__sm
        set $__i = GRANARY_META_LOG_LENGTH
      end
    end
    set $__i = $__i + 1
  end

  if $__m
    printf "Meta-data %p in group %lu:\n", $__m, $__g
    print-block-meta $__m
  end
  dont-repeat
end


# print-xed-reg
#
# Prints out a XED register, given the value of the register in `$arg0`.
define print-xed-reg
  set $__r = (xed_reg_enum_t) $arg0
  python None ; \
    m = gdb.execute("p $__r", from_tty=True, to_string=True) ; \
    m = re.sub(r"^\$.*= XED_REG_", "", m) ; \
    gdb.write(m.strip("\n")) ;
end


# print-xed-iform
#
# Prints out an instruction's IFORM, where `$arg0` is interpreted as a
# `xed_iform_enum_t`.
define print-xed-iform
  set $__iform = (xed_iform_enum_t) $arg0
  python None ; \
    m = gdb.execute("p $__iform", from_tty=True, to_string=True) ; \
    m = re.sub(r"^\$.*= XED_IFORM_", "", m) ; \
    gdb.write(m.strip("\n")) ;
end


# print-virt-reg
#
# Interprets `$arg0` as a `VirtualRegister` and prints it.
define print-virt-reg
  set language c++
  set $__vr = (granary::VirtualRegister) $arg0
  if VR_KIND_ARCH_FIXED == $__vr.kind
    print-xed-reg $__vr.reg_num
  end
  if VR_KIND_ARCH_GPR == $__vr.kind
    set $__r = $__vr.reg_num + XED_REG_RAX
    if XED_REG_RSP <= $__r
      set $__r = $__r + 1
    end
    if 0x3 == $__vr.byte_mask
      set $__r = $__r - (XED_REG_RAX - XED_REG_AX)
    end
    if 0xF == $__vr.byte_mask
      set $__r = $__r - (XED_REG_RAX - XED_REG_EAX)
    end
    if 0x1 == $__vr.byte_mask
      set $__r = $__r + (XED_REG_AL - XED_REG_RAX)
    end
    if 0x2 == $__vr.byte_mask
      set $__r = $__r + (XED_REG_AH - XED_REG_RAX)
    end
    print-xed-reg $__r
  end
  if VR_KIND_VIRTUAL_GPR == $__vr.kind || VR_KIND_VIRTUAL_STACK == $__vr.kind
    printf "%%%u", $__vr.reg_num
  end
  if VR_KIND_VIRTUAL_SLOT == $__vr.kind
    printf "SLOT:%u", $__vr.reg_num
  end
end


# print-arch-operand
#
# Prints the `arch::Operand` structure pointed to by `$arg0`.
define print-arch-operand
  set language c++
  set $__o = (granary::arch::Operand *) $arg0
  if XED_ENCODER_OPERAND_TYPE_BRDISP == $__o->type
    printf "0x%lx", $__o->addr.as_uint
  end

  # Memory operand
  if XED_ENCODER_OPERAND_TYPE_MEM == $__o->type
    printf "["

    # Segment displacement. Usually `XED_REG_DS`.
    if XED_REG_INVALID != $__o->segment && XED_REG_DS != $__o->segment
      print-xed-reg $__o->segment
      printf ":"
    end

    # Compound base + index * scale + displacement memory operand.
    if $__o->is_compound
      set $__pplus = 0
      if XED_REG_INVALID != $__o->mem.reg_base
        print-xed-reg $__o->mem.reg_base
        set $__pplus = 1
      end
      if XED_REG_INVALID != $__o->mem.reg_index
        if $__pplus
          printf " + "
        end
        print-xed-reg $__o->mem.reg_index
        printf " * %u", $__o->mem.scale
        set $__pplus = 1
      end
      if 0 != $__o->mem.disp
        if $__pplus
          printf " + "
        end
        printf "%d", $__o->mem.disp 
      end

    # Dereference of a register memory operand
    else
      print-virt-reg $__o->reg
    end
    printf "]"
  end

  # Print out a virtual register.
  if XED_ENCODER_OPERAND_TYPE_REG == $__o->type || \
     XED_ENCODER_OPERAND_TYPE_SEG0 == $__o->type || \
     XED_ENCODER_OPERAND_TYPE_SEG1 == $__o->type
    print-virt-reg $__o->reg
  end

  # Print out an immediate operand
  if XED_ENCODER_OPERAND_TYPE_IMM0 == $__o->type || \
     XED_ENCODER_OPERAND_TYPE_IMM1 == $__o->type || \
     XED_ENCODER_OPERAND_TYPE_SIMM0 == $__o->type
    if 0 > $__o->imm.as_int
      printf "-0x%lx", -$__o->imm.as_int
    else
      printf "0x%lx", $__o->imm.as_int
    end
  end

  # Print out some kind of pointer.
  if XED_ENCODER_OPERAND_TYPE_PTR == $__o->type
    printf "["
    if XED_REG_INVALID != $__o->segment && XED_REG_DS != $__o->segment
      print-xed-reg $__o->segment
      printf ":"
    end
    if $__o->is_annotation_instr
      printf "return address"
    else
      if 0 > $__o->addr.as_int
        printf "-0x%lx", -$__o->addr.as_int
      else
        printf "0x%lx", $__o->addr.as_int
      end
    end
    printf "]"
  end

  dont-repeat
end


# print-arch-instr-inline
#
# Interprets `$arg0` as being a pointer to an `arch::Instruction` structure, and
# prints the structure.
#
# Note: This function does not print a trialing new line character.
define print-arch-instr-inline
  set language c++
  set $__in = (granary::arch::Instruction *) $arg0
  print-xed-iform $__in->iform
  printf " "
  set $__op_num = 0
  while $__op_num < $__in->num_explicit_ops
    if 0 < $__op_num
      printf ", "
    end
    print-arch-operand &($__in->ops[$__op_num])
    set $__op_num = $__op_num + 1
  end
end


# print-arch-instr
#
# Interprets `$arg0` as being a pointer to an `arch::Instruction` structure, and
# prints the structure.
define print-arch-instr
  print-arch-instr-inline $arg0
  printf "\n"
  dont-repeat
end


# get-native-instr
#
# Treat `$arg0` as a pointer to an `Instruction`, and set `$__ni` to be a
# pointer to a `NativeInstruction` (or a subclass thereof) if `$arg0` is an
# instance of the right type.
define get-native-instr
  set language c++
  set $__i = (granary::Instruction *) $arg0
  set $__vi = ((char *) $__i->_vptr$Instruction) - 16
  if &_ZTVN7granary17NativeInstructionE == $__vi || \
     &_ZTVN7granary17BranchInstructionE == $__vi || \
     &_ZTVN7granary22ControlFlowInstructionE == $__vi
    set $__ni = (granary::NativeInstruction *) $__i
  else
    set $__ni = (granary::NativeInstruction *) 0
  end
end


# print-instr
#
# Treats `$arg0` as a pointer to an `Instruction` (not an `arch::Instruction`)
# and prints the instruction as an x86-like instruction if the instruction is
# an instance of `NativeInstruction`, otherwise nothing is printed.
define print-instr
  set language c++
  set $__i = (granary::Instruction *) $arg0
  get-native-instr $__i
  if $__ni
    print-arch-instr &($__ni->instruction)
  end
  dont-repeat
end


# get-next-instr
#
# Treat `$arg0` as a pointer to an `Instruction`, and set `$__i` to be a pointer
# to the next instruction.
define get-next-instr
  set language c++
  set $__i = (granary::Instruction *) $arg0
  set $__nil = $__i->list.next
  if $__nil
    set $__i = (granary::Instruction *) (((char *) $__nil) - 8)
  else
    set $__i = (granary::Instruction *) 0
  end
end


# get-next-frag
#
# Intepret `$arg0` as a pointer to a `Fragment`, and return a pointer to the
# next fragment by setting `$__f`.
define get-next-frag
  set language c++
  set $__f = (granary::Fragment *) $arg0
  if $__f->list.next
    set $__f = (granary::Fragment *) (((char *) $__f->list.next) - 8)
  else
    set $__f = (granary::Fragment *) 0
  end
end


# get-frag-partition
#
# Sets `$__p` to be a pointer to the `PartitionInfo` of a fragment `$arg0`.
define get-frag-partition
  set $__ps = &($arg0->partition)
  while $__ps->parent != $__ps
    set $__ps = $__ps->parent
  end
  set $__p = (granary::PartitionInfo *) $__ps->value
end


# print-frag-fillcolor
#
# Interpret `$arg0` as a pointer to some fragment's `PartitionInfo`, and use it
# to print the background color of the fragment.
define print-frag-fillcolor
  set language c++
  set $__p = (granary::PartitionInfo *) $arg0
  if $__p
    printf "%s", granary::os::fragment_partition_color[$__p->id % 11]
  else
    printf "white"
  end
end


# print-frag-instrs
#
# Treat `$arg0` as a pointer to a `Fragment`, and use it to print out the
# instructions of a fragment.
define print-frag-instrs
  set language c++
  set $__f = (granary::Fragment *) $arg0
  set $__i = $__f->instrs.first
  while $__i
    get-native-instr $__i
    if $__ni
      print-arch-instr-inline &($__ni->instruction)
      printf "<BR/>"
    end
    get-next-instr $__i
  end
end


# print-frag
#
# Treat `$arg0` as a pointer to a `Fragment` structure and do a simple printing
# of the fragment in an in-line fashion.
#
# Note: The printing format of the fragment is as a DOT digraph node.
define print-frag
  set language c++
  set $__f = (granary::Fragment *) $arg0
  get-frag-partition $__f

  printf "f%p [fillcolor=", $__f
  print-frag-fillcolor $__p

  printf " label=<{"
  
  if &_ZTVN7granary12CodeFragmentE == (((char *) $__f->_vptr$Fragment) - 16)
    set $__cf = (granary::CodeFragment *) $__f
    printf "head=%d, ", $__cf->attr.is_block_head
    printf "app=%d, ", granary::CODE_TYPE_APP == $__cf->type ? 1 : 0
    printf "addsucc2p=%d, ", $__cf->attr.can_add_succ_to_partition
    printf "stack=%d, ", granary::STACK_VALID == $__cf->stack.status ? 1 : 0
    printf "meta=%p", $__cf->attr.block_meta
    if $__p
      printf ", part=%d", $__p->id
    end
    printf "|"
  end
  print-frag-instrs $__f
  printf "}>];\n"
end


# print-frags
#
# Treat `$arg0` as a pointer to a `FragmentList`, and print the fragment list
# as a DOT digraph for later viewing from something like GraphViz / Xdot.py.
#
# Note: This will save the output to `/tmp/graph.dot`.
#
# Note: This operation will generally be VERY slow. You might need to babysit
#       it as GDB sometimes will prompt the user to continue after a lot of
#       output has been printed.
define print-frags
  set logging file /tmp/graph.dot
  set logging on
  printf ""
  set logging off
  set logging on
  set logging redirect on

  set language c++
  set $__fs = (granary::FragmentList *) $arg0
  set $__f = $__fs->first
  printf "digraph {\n"
  printf "node [fontname=Courier shape=record nojustify=false "
  printf    "labeljust=l style=filled];\n"
  printf "f0 [label=enter];"
  printf "f0 -> f%p;\n", $__f
  while $__f
    if $__f->successors[0]
      printf "f%p -> f%p;\n", $__f, $__f->successors[0]
    end
    if $__f->successors[1]
      printf "f%p -> f%p;\n", $__f, $__f->successors[1]
    end
    
    print-frag $__f
    get-next-frag $__f
  end
  printf "}\n"

  set logging redirect off
  set logging off

  printf "Opening /tmp/graph.dot with xdot...\n"
  shell xdot /tmp/graph.dot &
  dont-repeat
end


# get-next-block
#
# Treat `$arg0` as a pointer to a `BasicBlock`, and update the variable `$__b`
# to point to the next basic block in the list to which `$arg0` belongs.
define get-next-block
  set language c++
  set $__bl = (granary::ListHead *) (((unsigned long) $arg0) + 8)
  set $__nb = $__bl->next
  if $__nb
    set $__b = (granary::BasicBlock *) (((unsigned long) $__nb) - 8)
  else
    set $__b = (granary::BasicBlock *) 0
  end
end


# Saved machine state.
set $__reg_r15 = 0
set $__reg_r14 = 0
set $__reg_r13 = 0
set $__reg_r12 = 0
set $__reg_r11 = 0
set $__reg_r10 = 0
set $__reg_r9  = 0
set $__reg_r8  = 0
set $__reg_rdi = 0
set $__reg_rsi = 0
set $__reg_rbp = 0
set $__reg_rbx = 0
set $__reg_rdx = 0
set $__reg_rcx = 0
set $__reg_rax = 0
set $__reg_rsp = 0
set $__reg_eflags = 0
set $__reg_rip = 0
set $__regs_saved = 0


# Save all of the registers to some global GDB variables.
define save-regs
  set $__regs_saved = 1
  set $__reg_r15 = $r15
  set $__reg_r14 = $r14
  set $__reg_r13 = $r13
  set $__reg_r12 = $r12
  set $__reg_r11 = $r11
  set $__reg_r10 = $r10
  set $__reg_r9  = $r9 
  set $__reg_r8  = $r8 
  set $__reg_rdi = $rdi
  set $__reg_rsi = $rsi
  set $__reg_rbp = $rbp
  set $__reg_rbx = $rbx
  set $__reg_rdx = $rdx
  set $__reg_rcx = $rcx
  set $__reg_rax = $rax
  set $__reg_rsp = $rsp
  set $__reg_eflags = $eflags
  set $__reg_rip = $rip
  dont-repeat
end


# Restore all of the registers from some global GDB variables.
define restore-regs
  set $__regs_saved = 0
  set $r15 = $__reg_r15
  set $r14 = $__reg_r14
  set $r13 = $__reg_r13
  set $r12 = $__reg_r12
  set $r11 = $__reg_r11
  set $r10 = $__reg_r10
  set $r9  = $__reg_r9 
  set $r8  = $__reg_r8 
  set $rdi = $__reg_rdi
  set $rsi = $__reg_rsi
  set $rbp = $__reg_rbp
  set $rbx = $__reg_rbx
  set $rdx = $__reg_rdx
  set $rcx = $__reg_rcx
  set $rax = $__reg_rax
  set $rsp = $__reg_rsp
  set $eflags = $__reg_eflags
  set $rip = $__reg_rip
  dont-repeat
end


# restore-exec-entry <entry number>
#
# Restore the register state that was present at the time of the exec entry
# `$arg0`.
define restore-exec-entry
  set $__i = granary_block_log_index + GRANARY_BLOCK_LOG_LENGTH - $arg0
  set $__i = ($__i - 1) % GRANARY_BLOCK_LOG_LENGTH
  set $__regs = &(granary_block_log[$__i])

  # Save the current register state so that if we want to, we can
  # restore it later on to continue execution.
  if !$__regs_saved
    save-regs
  end

  set $r15 = $__regs->r15
  set $r14 = $__regs->r14
  set $r13 = $__regs->r13
  set $r12 = $__regs->r12
  set $r11 = $__regs->r11
  set $r10 = $__regs->r10
  set $r9  = $__regs->r9
  set $r8  = $__regs->r8
  set $rdi = $__regs->rdi
  set $rsi = $__regs->rsi
  set $rbp = $__regs->rbp
  set $rbx = $__regs->rbx
  set $rdx = $__regs->rdx
  set $rcx = $__regs->rcx
  set $rax = $__regs->rax
  set $rsp = $__regs->rsp
  set $eflags = (unsigned) $__regs->rflags
  set $rip = $__regs->rip
  if $in_user_space
    set $rip = $rip + 8
  end
end


# restore-ucontext <`struct ucontext` pointer>
#
# Restore the machine state designed by the Linux `struct ucontext` structure.
define restore-ucontext
  set $__regs = &(((struct ucontext *) $arg0)->uc_mcontext.gregs[0])

  # Save the current register state so that if we want to, we can
  # restore it later on to continue execution.
  if !$__regs_saved
    save-regs
  end

  set $r15 = $__regs[7]
  set $r14 = $__regs[6]
  set $r13 = $__regs[5]
  set $r12 = $__regs[4]
  set $r11 = $__regs[3]
  set $r10 = $__regs[2]
  set $r9  = $__regs[1]
  set $r8  = $__regs[0]
  set $rdi = $__regs[8]
  set $rsi = $__regs[9]
  set $rbp = $__regs[10]
  set $rbx = $__regs[11]
  set $rdx = $__regs[12]
  set $rcx = $__regs[14]
  set $rax = $__regs[13]
  set $rsp = $__regs[15]
  set $eflags = (unsigned) $__regs[17]
  set $rip = $__regs[16]
end


# restore-pt-regs <kernel `struct pt_regs` pointer>
#
# Restore the machine state described by the Linux kernel `struct pt_regs`.
define restore-pt-regs
  set $__regs = (struct pt_regs *) $arg0

  # Save the current register state so that if we want to, we can
  # restore it later on to continue execution.
  if !$__regs_saved
    save-regs
  end

  set $r15 = $__regs->r15
  set $r14 = $__regs->r14
  set $r13 = $__regs->r13
  set $r12 = $__regs->r12
  set $r11 = $__regs->r11
  set $r10 = $__regs->r10
  set $r9  = $__regs->r9
  set $r8  = $__regs->r8
  set $rdi = $__regs->di
  set $rsi = $__regs->si
  set $rbp = $__regs->bp
  set $rbx = $__regs->bx
  set $rdx = $__regs->dx
  set $rcx = $__regs->cx
  set $rax = $__regs->ax
  set $rsp = $__regs->sp
  set $eflags = $__regs->flags
  set $rip = $__regs->ip
end