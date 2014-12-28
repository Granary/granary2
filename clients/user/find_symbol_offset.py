"""Find the offset of a symbol in a shared library

Author:     Peter Goodman (peter.goodman@gmail.com)
Copyright:  Copyright 2014 Peter Goodman, all rights reserved."""

import glob
import re
import sys
import os

def get_files():
  a = list(glob.glob("/lib64/*"))
  b = list(glob.glob("/lib/x86_64-linux-gnu/*"))
  c = list(glob.glob("/usr/lib/x86_64-linux-gnu/*"))
  return a + b + c

def main(sym_name, lib_name):
  lib_name = lib_name.replace("+", "\\+")
  lib_name = lib_name.replace("*", "\\*")
  lib_name = lib_name.replace(".", "\\.")
  r = re.compile("^.*/" + lib_name + "(\.|-).*so(\.[0-9]+)*@?$")
  possibles = []
  for lib in get_files():
    if r.match(lib):
      possibles.append(lib)
  nice_lib_name = lib_name
  nice_lib_name = nice_lib_name.replace("+", "x")
  nice_lib_name = nice_lib_name.replace("*", "x")
  nice_lib_name = nice_lib_name.replace(".", "_")
  for lib in possibles:
    ret = os.popen("../user/find_symbol_offset.sh %s %s %s" % (sym_name, lib, nice_lib_name)).read()
    ret = ret.strip()
    if ret:
      print ret
      exit()

if "__main__" == __name__:
  main(sys.argv[1].strip(), sys.argv[2].strip())
