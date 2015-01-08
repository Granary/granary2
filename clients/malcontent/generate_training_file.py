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

    # char name[255]
    ba.extend(mod_name_bytes)
    ba.extend([b'\0'] * (256 - len(mod_name_bytes)))

    offsets = sorted(list(MODULES[mod]), key=lambda x: x[0])

    # uint64_t num_offsets
    ba.extend(struct.pack('<Q', len(offsets)))

    # uint64_t is_last_desc
    ba.extend(struct.pack('<Q', int(not num_mods)))

    # trailing TrainedOffsetDesc structures.
    for offset, accesses_shared_data in offsets:
      ba.extend(struct.pack('<L', offset))
      ba.extend(struct.pack('<L', accesses_shared_data))
    
  with open(sys.argv[2], "wb") as f:
    f.write(ba)

