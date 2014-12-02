"""Post-process the export headers.

Author:     Peter Goodman (peter.goodman@gmail.com)
Copyright:  Copyright 2014 Peter Goodman, all rights reserved."""

import os
import re
import sys

EXPORT_HEADERS = [
  "arch/base.h",
  "arch/context.h",
  "arch/cpu.h",
  "arch/util.h",

  "granary/base/abi.h",
  "granary/base/base.h",
  "granary/base/lock.h",
  "granary/base/cstring.h",
  "granary/base/new.h",
  "granary/base/tiny_vector.h",
  "granary/base/tiny_map.h",
  "granary/base/tiny_set.h",
  "granary/base/string.h",
  "granary/base/option.h",
  
  "granary/code/register.h",  # Ordering issues :-/
  "granary/metadata.h",  # Ordering issues :-/

  "granary/cfg/control_flow_graph.h",
  "granary/cfg/basic_block.h",
  "granary/cfg/instruction.h",
  "granary/cfg/factory.h",
  "granary/cfg/lir.h",
  "granary/cfg/operand.h",
  
  "granary/code/inline_assembly.h",

  "granary/app.h",
  "granary/breakpoint.h",
  "granary/client.h",
  "granary/entry.h",
  "granary/tool.h",
  "granary/util.h",

  "os/abi.h",
  "os/logging.h",
  "os/module.h",
  "os/lock.h",
]

OPEN_BRACE = re.compile("[^{]")
CLOSE_BRACE = re.compile("[^}]")
INCLUDE_SPACES = re.compile("^#([ ]*)")

# Open up a specific file to export, and combine its lines into a larger set of
# lines. Separate out system-level #includes.
def combine_file(combined_lines, system_headers, file_name):
  with open(file_name, "r") as lines:
    for line in lines:
      line = line.rstrip("\r\n\t ")
      if "include <" in line:
        if "valgrind" not in line and "new" not in line:
          system_headers.append(line)
      else:
        combined_lines.append(line)

# Combine together all files to be exported into a set of combined lines and
# system header #include directives.
def combine_output_files(source_dir):
  combined_lines = []
  system_headers = []
  for header_file in EXPORT_HEADERS:
    combine_file(
        combined_lines, system_headers, os.path.join(source_dir, header_file))
  combined_lines.append("")
  return (system_headers, combined_lines)

# Run the C pre-processor the combined lines.
def preprocess_combined_files(source_dir, lines, extra_flags):
  open("/tmp/granary_export0.h", "w").write("\n".join(lines))
  os.system(
      "clang++ -std=c++11 -I%s -DGRANARY_EXTERNAL %s "
      "-E -x c++ /tmp/granary_export0.h "
      "> /tmp/granary_export1.h" % (source_dir, extra_flags))
  os.system(
      "clang++ -std=c++11 -I%s -DGRANARY_EXTERNAL %s "
      "-E -dM -x c++ /tmp/granary_export0.h"
      " > /tmp/granary_export2.h" % (source_dir, extra_flags))
  os.system(
      "clang++ -std=c++11 -dM -E -x c++ /dev/null > /tmp/granary_export3.h")

# Filter the macro definitions that we want to export to exclude compiler-
# defined macros and include guards.
def filter_macros(file1, file2):
  with open(file1, "r") as lines1:
    with open(file2, "r") as lines2:
      lines1 = set(lines1)
      lines2 = set(lines2)
  lines = lines1 - lines2
  new_lines = []
  for line in lines:
    line = line.strip("\r\n\t ")
    if not line.endswith("_H_"):
      # Eclipse-aware macro renamers.
      if "IF_ECLIPSE_" in line:
        new_lines.append("#ifdef GRANARY_ECLIPSE")
        line = line.replace("IF_ECLIPSE_", "")
        new_lines.append(line)
        new_lines.append("#endif")

      elif "IF_NOT_ECLIPSE_" in line:
        new_lines.append("#ifndef GRANARY_ECLIPSE")
        line = line.replace("IF_NOT_ECLIPSE_", "")
        new_lines.append(line)
        new_lines.append("#endif")

      # Special case for this macro that is sometimes convenient for tricking
      # Eclipse.
      elif "GRANARY_IF_NOT_ECLIPSE" in line:
        new_lines.append("#ifdef GRANARY_ECLIPSE")
        new_lines.append("# define GRANARY_IF_NOT_ECLIPSE(...)")
        new_lines.append("#else")
        new_lines.append("# define GRANARY_IF_NOT_ECLIPSE(...)  __VA_ARGS__")
        new_lines.append("#endif")

      # Just a normal line.
      else:
        new_lines.append(line)
  return new_lines

# Return a list of unique system headers to #include.
def combine_system_headers(lines):
  return list(set(INCLUDE_SPACES.sub("#", inc) for inc in lines))

# Strip out internal Granary definitions and any remaining pre-processor
# directives from the pre-processed, combined files.
def strip_combined_files(new_lines):
  last_line_was_space = False
  in_internal_definition = False
  brace_count = 0
  with open("/tmp/granary_export1.h", "r") as lines:
    for line in lines:
      line = line.rstrip("\r\n\t ")
      if line.startswith("#"):  # Remove pre-processor file/line associations.
        continue

      # Don't reveal internals about abstraction-breaking relationships among
      # Granary's internal classes/functions.
      if line.lstrip("\t ").startswith("friend "):
        continue

      # Make sure that certain uses of friend classes/functions are exported.
      line = line.replace("GRANARY_EXPORTED_friend", "friend")

      if "GRANARY_INTERNAL_DEFINITION" in line:
        in_internal_definition = True
        brace_count = 0

      if in_internal_definition:
        brace_count += len(OPEN_BRACE.sub("", line))
        had_braces = 0 < brace_count
        brace_count -= len(CLOSE_BRACE.sub("", line))

        if not brace_count and (had_braces or (line and ";" == line[-1])):
          in_internal_definition = False
        continue
      
      if not line:  # Merge consecutive empty lines into a single empty line.
        if not last_line_was_space:
          new_lines.append("")
          last_line_was_space = True
        continue
      else:
        last_line_was_space = False
        new_lines.append(line)

def main(where, source_dir, export_dir, extra_flags):
  system_includes, lines = combine_output_files(source_dir)
  preprocess_combined_files(source_dir, lines, extra_flags)
  new_lines = ["#ifndef GRANARY_H_"]
  new_lines.append("#define GRANARY_H_")
  new_lines.extend(filter_macros("/tmp/granary_export2.h",
                                 "/tmp/granary_export3.h"))
  new_lines.extend(combine_system_headers(system_includes))
  strip_combined_files(new_lines)
  new_lines.append("#endif  // GRANARY_H_")
  new_lines.append("")  
  open(os.path.join(export_dir, "granary.h"), "w").write(
      "\n".join(new_lines))

if "__main__" == __name__:
  main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])
