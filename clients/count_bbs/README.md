count_bbs
=========

This tool is able to do the following:
   
  1. Count the number of decoded basic blocks in a program.
    
  **Note:** This might over-estimate the true number of blocks.
  2. Count the number of executions of each block in a program.

  3. Count the number of executions of each block in a program, with respect to
    the last executed conditional branch within the current
    function/procedure.

### Example Usage

#### Counting the number of decoded blocks

```
/path/to/granary> ./bin/debug_linux_user/grr --tools=count_bbs -- ls
Process ID for attaching GDB: 32352
Press enter to continue.

arch  bin  Client.inc  clients  dependencies  generated  granary  __init__.py  linker.lds  Makefile  Makefile.inc  os  qemu.img  README.md  scripts  symbol.exports  symbol.versions  test  vmlinux
#count_bbs 1429 blocks were translated.
/path/to/granary>
```

As you can see, `1429` blocks were decoded and executed.

#### Counting the number of executions of each block

```
/path/to/granary> ./bin/debug_linux_user/grr --tools=count_bbs --count_execs -- ls
Process ID for attaching GDB: 318
Press enter to continue.

arch  bin  Client.inc  clients  dependencies  generated  granary  __init__.py  linker.lds  Makefile  Makefile.inc  os  qemu.img  README.md  scripts  symbol.exports  symbol.versions  test  vmlinux
B libc 7c008 C 1
B libc 7c018 C 1
B libc 7c025 C 0
...
B libc 88f32 C 1
B libc 88f40 C 64
B libc 88f70 C 0
B libc 892a0 C 14
B libc 892ae C 0
B libc 892bf C 14
B libc 892ba C 0
B libc 89540 C 259
B libc 8956a C 259
B libc 8957d C 258
...
B libc 7bca0 C 36
B libc 7b240 C 0
B libc 7b3d8 C 1
B libc 7b240 C 41
#count_bbs 1429 blocks were translated.
/path/to/granary>
```

Here, we see that again, `1429` were decoded, and for each of those blocks, we output
line of the form `B <library short name> <offset in library> C <num executions>`.

This information can be used for simple code coverage analysis, as well as identifying
what code is hot.

#### Counting arc-specific executions of each block

```
/path/to/granary> ./bin/debug_linux_user/grr --tools=count_bbs --count_execs --count_per_condition -- ls
Process ID for attaching GDB: 831
Press enter to continue.

arch  bin  Client.inc  clients  dependencies  generated  granary  __init__.py  linker.lds  Makefile  Makefile.inc  os  qemu.img  README.md  scripts  symbol.exports  symbol.versions  test  vmlinux
B ld 130a A 0 C 0
B ld 1330 A 0 C 0
B ld 1340 A 0 C 0
B ld 100a0 A 0 C 1
B ld 100fd A 0 C 0
B ld 10143 A c0cc C 1
B ld 10143 A c107 C 1
B ld 1019c A c0cc C 1
B ld 101a4 A c0cc C 1
...

B libc c3869 A 0 C 12
B libc c3864 A e700 C 12
B libc ca9c2 A 0 C 1
B libc cbd43 A 0 C 1
B libc cbcbb A 0 C 1
B libc cbd30 A 0 C 1
B libc ed9bc A 0 C 1
B libc ed9b0 A 0 C 1
B libc f1db0 A 0 C 1
#count_bbs 1673 blocks were translated.
/path/to/granary>
```

This is slightly different. First, we see that more blocks (`1673`) were translated than in other examples. This is
because multiple versions of the sample native block are sometimes created. This happens in when the same native 
block is dynamically preceded by one or more conditional branches.

The output format here is `B <library short name> <offset in library> A <identifier for branch instruction in function> C <count>`.
This output can be used for mildly path-sensitive code coverage analysis.
