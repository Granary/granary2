"""Generate watchpoint wrappers for every system call that has associated
type info.

Author:     Peter Goodman (peter.goodman@gmail.com)
Copyright:  Copyright 2014 Peter Goodman, all rights reserved."""

import re
import sys
from dependencies.cparser.cparser import *
from dependencies.cparser.cprinter import pretty_print_type

SYSCALL_NAME = re.compile(r"__NR_([a-zA-Z0-9_]+)")
STRUCT_WRAPPERS = {}

# Maps (system call names, type names) to relative offset of the array length
# argument. So, in the case of:
#   `ssize_t readv(int , const struct iovec *__iovec, int __count)`
#
# We have `(readv, struct iovec) => 1` because the `__count` is at position
# `2`, which is a `1`-displaced from the position of the `__iovec` parameter. 
ARRAY_CTYPES = {
  ("readv", "struct iovec"): 1,
  ("writev", "struct iovec"): 1,
  ("preadv", "struct iovec"): 1,
  ("pwritev", "struct iovec"): 1,
}

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

def wrap_array_of_struct(ctype_printed, counter_ctype_printed, i, j):
  return "WRAP_SYSCALL_ARG_ASTRUCT(%d,%d,%s,%s)" % (i, j, ctype_printed,
                                                    counter_ctype_printed)

# Generates a structure wrapper. Returns `True` if a structure wrapper is
# generated, and `False` otherwise. Structure wrappers aren't generated if all
# we have is a forward declaration of the structure.
def make_struct_wrapper(ctype, ctype_printed):
  global STRUCT_WRAPPERS
  if ctype in STRUCT_WRAPPERS:
    return STRUCT_WRAPPERS[ctype]

  actions = []
  for (field_ctype, field_name) in ctype.fields():
    field_ctype = field_ctype.base_type()
    if isinstance(field_ctype, CTypePointer) \
    and not is_function_pointer(field_ctype):
      actions.append("WRAP_STRUCT_PFIELD(%s)" % field_name)

  if actions:
    print "WRAP_STRUCT(%s,%s)" % (ctype_printed, ";".join(actions))
    STRUCT_WRAPPERS[ctype] = True
    return True
  else:
    STRUCT_WRAPPERS[ctype] = False
    return False

# If this is the first call, then this will print out a wrapper macro for the
# structure itself. All calls return a string that tells the system call wrapper
# how to wrap this structure.
def wrap_struct(syscall_name, i, ctype, param_ctypes):
  global STRUCT_WRAPPERS
  global ARRAY_CTYPES

  ctype_printed = pretty_print_type(ctype)
  if make_struct_wrapper(ctype, ctype_printed):
    array_key = (syscall_name, ctype.name)
    if array_key in ARRAY_CTYPES:
      j = ARRAY_CTYPES[array_key] + i
      return wrap_array_of_struct(ctype_printed,
                                  pretty_print_type(param_ctypes[j]), i, j)
    else:
      return "WRAP_SYSCALL_ARG_PSTRUCT(%d, %s)" % (i,ctype_printed)
  else:
    return wrap_pointer(i)

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
      actions.append(wrap_struct(var, arg_num, pointed_ctype,
                                 ctype.param_types))
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
