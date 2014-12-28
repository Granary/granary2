readelf -Ws $2 | grep " $1\(@.*\)*$" | sed -n "s/^.*: \([0-9a-f]*\).*/#define SYMBOL_OFFSET_$3_$1 0x\1ull/p"
