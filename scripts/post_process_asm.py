"""Post-process an assembly file and do simple symbol concatenation 
(in case the assembly pre-processor does not support it) and replace
the @N@ symbol with a new line.

Author:     Peter Goodman (peter.goodman@gmail.com)
Copyright:  Copyright 2014 Peter Goodman, all rights reserved.
"""

import sys
import re

NL = re.compile(r"@N@")
INDENT = re.compile(r"^([ \t]*)")
PASTE = re.compile(r"(.*?)\s*##\s*(.*?)")

# Open up a post-processed assembly file and replace each @N@ with a new line
# and if the C pre-processor didn't do token pasting, then automatically perform
# it.
new_lines = []
with open(sys.argv[1], "r") as lines:
  for line in lines:

    if line.startswith("#"):
      continue

    # Continue the same indentation with @N@ to try to improve the readability
    # of the post-processed assembly.
    indent = INDENT.match(line)
    if not indent:
      indent = ""
    else:
      indent = str(indent.group(1))

    line = NL.sub("%s\n" % indent, line.rstrip(" \t\r\n"))

    # Symbol concatenation.
    while True:
      old_line = line
      line = PASTE.sub(r"\1\2", line)
      if old_line == line:
        break

    new_lines.append(line.rstrip("\n"))

# Overwrite the file in-place.
with open(sys.argv[1], "w") as lines:
  lines.write("\n".join(new_lines))