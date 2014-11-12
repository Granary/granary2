"""Generate watchpoint wrappers for every system call that has associated
type info.

Author:     Peter Goodman (peter.goodman@gmail.com)
Copyright:  Copyright 2014 Peter Goodman, all rights reserved."""

import re
import sys
from dependencies.cparser.cparser import *
from dependencies.cparser.cprinter import pretty_print_type

SYSCALL_NAME = re.compile(r"__NR_([a-zA-Z0-9_]+)")
TYPE_WRAPPERS = {}

# This system call has no pointer arguments, so don't wrap it.
def dont_wrap_syscall(var):
  print "NO_WRAP_SYSCALL(%s)" % var

# We don't have type information for this system call, so we'll spit out an
# instruction to do best-effort wrapping, i.e. treat all 6 potential arguments
# as pointers and wrap them.
def generic_wrap_syscall(var):
  print "GENERIC_WRAP_SYSCALL(%s)" % var

# We know argument `i` is a pointer, so try to wrap it (i.e. untaint it).
def wrap_pointer(i):
  return "WRAP_SYSCALL_ARG_POINTER(%d)" % i

# Returns true if `ctype` represents a function pointer.
def is_function_pointer(ctype):
  if isinstance(ctype, CTypePointer):
    return isinstance(ctype.ctype.base_type(), CTypeFunction)
  return False

# Generates a system call structure wrapper. If this is the first call, then
# this will print out a wrapper macro for the structure itself. All calls
# return a string that tells the system call wrapper how to wrap this
# structure.
def wrap_struct(i, ctype):
  ctype_printed = pretty_print_type(ctype)
  ret = "WRAP_SYSCALL_ARG_PSTRUCT(%d, %s)" % (i, ctype_printed)

  global TYPE_WRAPPERS
  if ctype in TYPE_WRAPPERS:
    return TYPE_WRAPPERS[ctype]

  actions = []
  for (field_ctype, field_name) in ctype.fields():
    field_ctype = field_ctype.base_type()
    if isinstance(field_ctype, CTypePointer) \
    and not is_function_pointer(field_ctype):
      actions.append("WRAP_STRUCT_PFIELD(%s)" % field_name)

  # If we only have a forward declaration of the structure, then we can't
  # generate a wrapper for it. Similarly, if the structure has no pointer
  # fields, then we also don't wrap it.
  if not len(actions):
    ret = wrap_pointer(i)
  else:
    print "WRAP_STRUCT(%s,%s)" % (ctype_printed, ";".join(actions))

  TYPE_WRAPPERS[ctype] = ret
  return ret

# Wrap a system call named `var`, where the have the function prototype `ctype`
# of the system call.
#
# This goes over each system call argument and tries to wrap pointers. 
def wrap_syscall(var, ctype):
  ctype = ctype.unattributed_type()
  if not isinstance(ctype, CTypeFunction):
    return dont_wrap_syscall(var)
  
  actions = []
  arg_num = -1
  for param_ctype in ctype.param_types:
    arg_num += 1
    param_ctype = param_ctype.base_type()
    if not isinstance(param_ctype, CTypePointer) \
    or is_function_pointer(param_ctype):
      continue
    
    pointed_ctype = param_ctype.ctype.base_type()
    if isinstance(pointed_ctype, CTypeStruct):
      actions.append(wrap_struct(arg_num, pointed_ctype))
    else:
      actions.append(wrap_pointer(arg_num))

  if not actions:
    return dont_wrap_syscall(var)

  print "WRAP_SYSCALL(%s, %s)" % (var, ";".join(actions))

# Given a file that should contain type information about system calls, as well
# as macros that define the individual system call numbers, build up a list
# of what system calls are supported, then try to wrap those system calls in
# a type-specific way.
def main(types_file_name):
  syscall_names = set()
  line_buff = []
  with open(types_file_name, "r") as lines:
    for line in lines:
      if line.startswith("#"):
        m = SYSCALL_NAME.search(line)
        if m:
          syscall_names.add(m.group(1))
      else:
        line_buff.append(line)

  tokens = CTokenizer("".join(line_buff))
  parser = CParser()
  parser.parse(tokens)
  for var, ctype in parser.vars():
    if var in syscall_names:
      wrap_syscall(var, ctype)
      syscall_names.remove(var)

  for var in syscall_names:
    generic_wrap_syscall(var)

if "__main__" == __name__:
  main(sys.argv[1])
