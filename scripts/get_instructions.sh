# Given a vmlinux file, this disassembles the file, strips out various junk
# from the disassembly, then tries to print out only those unique instruction
# encodings from vmlinux.
#
# Note: Some manual post-processign is usually required as some of the
#       disassembled instructions won't be valid (e.g. include manu unusual
#       prefix combinations for a given instruction).
objdump --no-show-raw-insn --disassemble "$1" | \
grep '^[ ]*[a-f0-9]*:' | \
sed 's/\#.*//' | \
sed 's/.*\t//' | \
sed 's/<.*//' | \
sed 's/[ \t]*$//' | \
grep -v 'ffffffff8' | \
grep -v '^j.* [a-f0-9]*[ ]*' | \
grep -v '^call.* [a-f0-9]*[ ]*' | \
grep -v 'bad' | \
sort | \
uniq -u > \
"$2"
