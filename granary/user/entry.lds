SECTIONS {
  .ctors : { *(SORT(.ctors)) entry.o(SORT(.ctors)) }
}