poly_code
=========

This tool looks for code polymorphism. Specifically, it assigns type IDs to
heap-allocated objects, and then propagates those type IDs to blocks that access
the memory of those objects.

This is useful when we want to know what code accesses what kind of data. The
output of this tool is used as "training" input to the `malcontent` cache line
contention/sharing detector.

### Example Usage

#### Getting type information for each block

```
/path/to/granary> ./bin/opt_linux_user/grr --tools=poly_code -- ls
Process ID for attaching GDB: 1725
Press enter to continue.

arch  bin  Client.inc  clients  dependencies  generated  granary  __init__.py  linker.lds  Makefile  Makefile.inc  os  qemu.img  README.md  scripts  symbol.exports  symbol.versions  test  vmlinux
T 0 2 B libc 34545
T 1 6 B libc 2e936
T 2 3 B libc 892ba
T 3 9 B libc 2dc60
T 4 6 B libc 2dc60
T 5 7 B libc 2dc60
T 6 8 B libc 2dc60
T 7 3 B libc 2ce5c
T 8 5 B libc 30161
T 9 5 B ls 10c49
T 10 14 B ls 10c49
T 11 1 B ls 10c49
T 12 15 B libc bd851
T 13 3 B ls 10c49
T 14 4 B ls 10c49
T 15 2 B ls 10c49
T 16 8 B ls 10c49
T 17 12 B ls 10c49
B libc 1f410
B libc 1f420
B libc 1f470
B libc 21dd0
...
B libc 2e969 Ts 1
B libc 2e990
B libc 2e99c
B libc 2e9a1
B libc 2e9b6
B libc 2e9c7 Ts 1,3,4,5,6
B libc 2e9e2 Ts 1
B libc 2ea43
...
B libgranary 2fc1fe
B libgranary 2fc1fe
/path/to/granary>
```

Here, we see two distinct kinds out output. The first kind of output describes
identified "types" in terms of a `(allocation size, allocator return address)`
pair. The output format is `T <type id> <size in bytes> B <library short name> <offset of return address in library>`.
The second output format describes what blocks were translated, and what types were
accessed by those blocks. The output format is `B <library short name> <library offset> [Ts <type id> ...]`.
The absence of a `Ts ...` implies that no dynamically allocated memory was accessed in the
block.

**Note:** `poly_code` recognizes dynamic memory allocations in terms of common `libc` and
`libc++` functions likes `malloc` and `operator new`. If other functions/libraries are being
used then the `poly_code` tool might not know that such 

#### Getting summary information for each block

Another way to run the `poly_code` tool and to get similar but less precise
information is by disabling the `--record_block_types` option. For example:

```
/path/to/granary> ./bin/opt_linux_user/grr --tools=poly_code --no_record_block_types -- ls
Process ID for attaching GDB: 1964
Press enter to continue.

arch  bin  Client.inc  clients  dependencies  generated  granary  __init__.py  linker.lds  Makefile  Makefile.inc  os  qemu.img  README.md  scripts  symbol.exports  symbol.versions  test  vmlinux
B libgranary 303662
B ls 2168
...
B ls 346d
B ls 34a1 Ts *
B ls 34ae
B ls 34b7
B ls 34bf
B ls 34d2
B ls 34de Ts *
B ls 3508 Ts *
B ls 351e
...
B ld 1ae10
B ld 1a995
B ld 1ae40
/path/to/granary>
```

Here, we no longer see lines defining type IDs, nor do we see specific type IDs for each block.
Instead, we see `Ts *` for some blocks, which says "this block accesses dynamically allocated data", but
does not tell us what specific types of data are accessed.

#### Converting `poly_code` output into a training file for `malcontent`

The output of the `poly_code` tool can be used as a training input file for
the `malcontent` cache line contention/sharing detector. For example:

```
/path/to/granary> ./bin/opt_linux_user/grr --tools=poly_code --no_record_block_types --output_log_file=/tmp/training.log -- ls
Process ID for attaching GDB: 2367
Press enter to continue.

arch  bin  Client.inc  clients  dependencies  generated  granary  __init__.py  linker.lds  Makefile  Makefile.inc  os  qemu.img  README.md  scripts  symbol.exports  symbol.versions  test  vmlinux
/path/to/granary> python clients/malcontent/generate_training_file.py /tmp/training.log /tmp/training.bin 
```

Now, the training file is stored in `/tmp/training.bin`, and can be used by `malcontent` as follows:

```
/path/to/granary> ./bin/opt_linux_user/grr --tools=malcontent --sample_training_file=/tmp/training.bin -- ls 
Process ID for attaching GDB: 2648
Press enter to continue.

arch  bin  Client.inc  clients  dependencies  generated  granary  __init__.py  linker.lds  Makefile  Makefile.inc  os  qemu.img  README.md  scripts  symbol.exports  symbol.versions  test  vmlinux
```
