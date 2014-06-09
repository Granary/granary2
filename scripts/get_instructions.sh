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
