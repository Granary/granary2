Granary+
========

Setup
-----

### Step 1: Initial setup.

```basemake
./scripts/make_vmlinux_link.sh <path-to-kernel-vmlinux>
./scripts/make_qemu_img_link.sh <path-to-QEMU-VM-image>
```

### Step 2: Compiling Granary.
#### User Space
If you are compiling Granary for user space, run:

```basemake
make clean ; make all
```

To run Granary on a binary, e.g. `ls`, do:

```basemake
./bin/debug_user/grr -- ls
```

If you want to compile Granary in standalone mode (where it will not take over
some binary's execution via `LD_PRELOAD`, do:

```basemake
make clean ; make all GRANARY_STANDALONE=1
```

Then you can run:

```basemake
./bin/debug_user/granary.out
```

#### Kernel Space

If you are compiling Granary against your running kernel, run:

```basemake
make clean all GRANARY_WHERE=kernel
```

If you are compiling Granary against a custom kernel, run:

```basemake
make clean all GRANARY_WHERE=kernel GRANARY_KERNEL_DIR=<path-to-kernel-source>
```

### Step 3: Compiling tools.
#### User Space

```basemake
make clean_tools tools GRANARY_TOOLS="follow_jumps count_bbs print_bbs"
./bin/debug_user/grr --tools=follow_jumps,count_bbs,print_bbs -- ls
```

#### Kernel Space

If you are compiling Granary tools against a custom kernel, run:

```basemake
make clean_tools tools GRANARY_TOOLS="follow_jumps count_bbs print_bbs" GRANARY_WHERE=kernel GRANARY_KERNEL_DIR=<path-to-kernel-source>
```
