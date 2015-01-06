"""Post-process the output of the `poly_code` client and use it to generate
a file for use by the `malcontent` tool that will restrict what code is
heavily instrumented.

Author:     Peter Goodman (peter.goodman@gmail.com)
Copyright:  Copyright 2015 Peter Goodman, all rights reserved."""

import collections
import struct
import os
import sys

MODULES = collections.defaultdict(set)

if "__main__" == __name__:
  with open(sys.argv[1], "r") as poly_code_lines:
    for line in poly_code_lines:
      if not line.startswith("B"):
        continue
      parts = line.split(" ")
      offs = int(parts[2], 16)

      if "Ts" in line:
        MODULES[parts[1]].add((offs, 1))
      else:
        MODULES[parts[1]].add((offs, 0))

  ba = bytearray()

  num_mods = len(MODULES)
  for mod in MODULES:
    num_mods -= 1

    mod_name_bytes = bytes(mod)
    ba.extend(mod_name_bytes)
    ba.extend([b'\0'] * (256 - len(mod_name_bytes)))

    offset_ba = bytearray()
    offsets = sorted(list(MODULES[mod]), key=lambda x: x[0])
    for offset, accesses_shared_data in offsets:
      offset_ba.extend(struct.pack('<L', offset))
      offset_ba.extend(struct.pack('<L', accesses_shared_data))
    curr = len(ba)
    ba.extend(struct.pack('<Q', len(offsets)))
    
    if num_mods:
      ba.extend(struct.pack('<Q', 0))
    else:
      ba.extend(struct.pack('<Q', 1))

    ba.extend(offset_ba)

  with open(sys.argv[2], "wb") as f:
    f.write(ba)

