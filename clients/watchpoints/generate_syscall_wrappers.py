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
#
# Also maps structure fields to array length fields.
ARRAY_CTYPES = {
  # (syscall name, type name) -> array count argument offset
  ("readv", "struct iovec"): 1,
  ("writev", "struct iovec"): 1,
  ("preadv", "struct iovec"): 1,
  ("pwritev", "struct iovec"): 1,

  # (structure name, field name) -> array count field name
  ("struct msghdr", "msg_iov"): "msg_iovlen",
}

# Returns true if `ctype` represents a function pointer.
def is_function_pointer(ctype):
  if isinstance(ctype, CTypePointer):
    return isinstance(ctype.ctype.base_type(), CTypeFunction)
  return False

# If we have a union with pointers in it, such as with `__SOCKADDR_ARG` being
# passed to `getpeername`, then return one of the pointer types.
def pointer_ctype_in_union(ctype):
  if not isinstance(ctype, CTypeUnion):
    return None
  for field_ctype, name in ctype.fields():
    field_ctype = field_ctype.base_type()
    if isinstance(field_ctype, CTypePointer):
      return field_ctype
  return None

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

# This system call argument, when combined with another argument, represent
# an fixed-length array of structures, all of which should themselves be
# wrapped.
def wrap_array_of_struct(ctype_printed, counter_ctype_printed, i, j):
  return "WRAP_SYSCALL_ARG_ASTRUCT(%d,%d,%s,%s)" % (i, j, ctype_printed,
                                                    counter_ctype_printed)

# Generates a structure wrapper. Returns `True` if a structure wrapper is
# generated, and `False` otherwise. Structure wrappers aren't generated if all
# we have is a forward declaration of the structure.
def make_struct_wrapper(ctype, ctype_printed):
  global STRUCT_WRAPPERS
  global ARRAY_CTYPES

  if ctype in STRUCT_WRAPPERS:
    return STRUCT_WRAPPERS[ctype]

  actions = []
  for (field_ctype, field_name) in ctype.fields():
    field_ctype = field_ctype.base_type()
    if isinstance(field_ctype, CTypePointer) \
    and not is_function_pointer(field_ctype):
      # Establish a base case, just in case we get `A.x -> ... -> A` and loop
      # infinitely when trying to generate a struct wrapper for `A`.
      STRUCT_WRAPPERS[ctype] = True

      # Try to see if we explicitly recognize this structure field as being
      # a pointer to an array of structures, where the size of the array is
      # defined by another field in `ctype`.
      array_key = (ctype_printed, field_name)
      if array_key in ARRAY_CTYPES:
        len_field = ARRAY_CTYPES[array_key]
        field_ptype = field_ctype.ctype.base_type()
        is_struct = make_struct_wrapper(field_ptype,
                                        pretty_print_type(field_ptype))
        assert is_struct
        actions.append("WRAP_STRUCT_ASTRUCT(%s,%s)" % (field_name,len_field))

      # Just a regular pointer field.
      else:
        actions.append("WRAP_STRUCT_PFIELD(%s)" % field_name)

  if actions:
    print "WRAP_STRUCT(%s,%s)" % (ctype_printed, ";".join(actions))
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
    if isinstance(param_ctype, CTypePointer):
      if is_function_pointer(param_ctype):
        continue
    elif not isinstance(param_ctype, CTypeArray):
      union_field_type = pointer_ctype_in_union(param_ctype)
      if not union_field_type:
        continue
      else:
        param_ctype = union_field_type
    
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

  tokens = CTokenizer(line_buff)
  parser = CParser()
  for decls, _, _ in parser.parse_units(tokens):
    for ctype, var in parser.vars():
      if var in syscall_names:
        wrap_syscall(var, ctype)
        syscall_names.remove(var)

  for var in syscall_names:
    generic_wrap_syscall(var)

if "__main__" == __name__:
  main(sys.argv[1])
