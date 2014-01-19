set logging off
set breakpoint pending on
set print demangle on
set print asm-demangle on
set print object on
set print static-members on
set disassembly-flavor att
set language c++

catch throw
b granary_break_on_fault
b granary_break_on_unreachable_code
b granary_break_on_encode
b granary_break_on_decode