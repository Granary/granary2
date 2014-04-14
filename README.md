Granary+
========

Setup
-----

### Step 0: Make sure you have everything that you need.

1. Get LLVM and clang:
  ```basemake
  sudo apt-get install llvm libc++-src clang-3.4
  ```

2. Make sure you have Python 2.7 or above, but not Python 3. 

### Step 1: Initial setup.

```basemake
./scripts/make_vmlinux_link.sh <path-to-kernel-vmlinux>
./scripts/make_qemu_img_link.sh <path-to-QEMU-VM-image>
```

### Step 2: Compiling Granary.
#### User Space
If you are compiling Granary for user space, run:

```basemake
make clean all
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
make clean_clients clients GRANARY_CLIENTS="follow_jumps print_bbs"
./bin/debug_user/grr --clients=follow_jumps,print_bbs --tools=follow_jumps,print_bbs -- ls
```

#### Kernel Space

If you are compiling Granary clients (and their tools) against a custom kernel, run:

```basemake
make clean_clients clients GRANARY_CLIENTS="follow_jumps print_bbs" GRANARY_WHERE=kernel GRANARY_KERNEL_DIR=<path-to-kernel-source>
```
