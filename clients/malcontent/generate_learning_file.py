"""Post-process the output of the `poly_code` client and use it to generate
a file for use by the `malcontent` tool that will restrict what code is
heavily instrumented.

Author:     Peter Goodman (peter.goodman@gmail.com)
Copyright:  Copyright 2015 Peter Goodman, all rights reserved."""

import collections
import mmap
import os
import sys

MODULES = collections.defaultdict(set)

if "__main__" == __name__:
  with open(sys.argv[1], "r") as poly_code_lines:
    for line in poly_code_lines:
      if not line.startswith("B"):
        continue
      if not "Ts" in line:
        continue
      parts = line.split(" ")
      MODULES[parts[1]].add(int(parts[2], 16))
  

