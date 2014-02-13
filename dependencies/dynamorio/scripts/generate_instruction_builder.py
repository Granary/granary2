"""Generate an instruction builder interface for Granary to use.

Author:     Peter Goodman (peter.goodman@gmail.com)
Copyright:  Copyright 2014 Peter Goodman, all rights reserved.
"""

import os
import re
import sys

SPLIT_INSTR = re.compile(
    r"^\#define INSTR_CREATE_([a-zA-Z0-9_]+)\((.*)\) (.*)$")

def line_outputter(output_file):
  def output(*args):
    output_file.write("%s\n" % "".join(str(a) for a in args))
  return output

def both(func1, func2):
  def func(*args):
    func1(*args)
    func2(*args)
  return func

def format_argument(opcode, arg_name):
  if "dc" == arg_name:  # All instructions take a dcontext.
    return "InstructionDecoder *dc"
  elif "n" == arg_name:  # NOPNBYTE
    return "unsigned n"
  elif "CC" in opcode and "op" == arg_name:  # JCC, JCC_SHORT, SETCC
    return "unsigned op"
  else:  # The rest are plain old operands.
    return "dynamorio::opnd_t %s" % arg_name

def process_instr(opcode, args, impl, output_cc, output_h):
  if not impl.startswith("instr_create_") or "float" in impl:
    return

  opcode = opcode.upper()
  arg_string = ", ".join(format_argument(opcode, a) for a in args[1:]) or "void"

  # Declaration
  output_h(
      "  DecodedInstruction *", opcode, "(", arg_string, ");")

  # Definition
  output_cc(
      "DecodedInstruction *InstructionBuilder::", opcode, "(",
      arg_string, ") {")
  output_cc("  auto instr = new DecodedInstruction;")
  output_cc("  decoder.in_flight_instruction = instr;")

  # Fixup some of the originally C constants to use their Granary equivalents.
  impl = impl.replace("OP_", "dynamorio::OP_")
  impl = impl.replace("DR_", "dynamorio::DR_")  # Registers.
  impl = impl.replace("REG_X", "REG_R")  # 64-bit registers.
  impl = impl.replace("REG_RMM", "REG_XMM")  # Undo screwup.
  impl = impl.replace("OPSZ", "dynamorio::OPSZ")
  impl = impl.replace("(dc)", "&decoder")  # Use the decoder as the dcontext.
  impl = impl.replace("(dc,", "(&decoder,")  # Use the decoder as the dcontext.
  impl = impl.replace("opnd_create", "dynamorio::opnd_create")

  output_cc("  dynamorio::", impl, ";")
  output_cc("  decoder.in_flight_instruction = nullptr;")
  output_cc("  return instr;")
  output_cc("}\n")

def process_line(line, output_cc, output_h):
  if "GRANARY_IGNORE" not in line and "(" in line and "RAW_" not in line:
    parts = SPLIT_INSTR.match(line)
    process_instr(
        parts.group(1), parts.group(2).split(","), parts.group(3),
        output_cc, output_h)

def main(input_file, output_dir):
  output_cc = line_outputter(open(os.path.join(output_dir, "builder.cc"), "w"))
  output_h = line_outputter(open(os.path.join(output_dir, "builder.h"), "w"))
  
  both(output_h, output_cc)("/* Auto-generated file. DO NOT CHANGE! */\n")
  output_cc("#define GRANARY_INTERNAL\n")
  output_h("""#include "granary/driver/dynamorio/decoder.h"\n""")
  output_cc("""#include "generated/dynamorio/builder.h"\n""")
  output_h("""#include "granary/driver/dynamorio/instruction.h"\n""")
  both(output_h, output_cc)("namespace granary {")
  both(output_h, output_cc)("namespace driver {")
  both(output_h, output_cc)()
  output_h("// Forward declarations.")
  output_h("class DecodedInstruction;")
  output_h()
  output_h("class InstructionBuilder {")
  output_h(" public:")
  output_h("  InstructionBuilder(void) = default;")

  with open(input_file, "r") as lines:
    for line in lines:
      process_line(line.strip("\r\n\t "), output_cc, output_h)

  output_h(" private:")
  output_h("  InstructionDecoder decoder;")
  output_h("  GRANARY_DISALLOW_COPY_AND_ASSIGN(InstructionBuilder);")
  output_h("};  ")
  both(output_h, output_cc)("}  // namespace driver")
  both(output_h, output_cc)("}  // namespace granary")

if "__main__" == __name__:
  main(sys.argv[1], sys.argv[2])