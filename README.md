Granary+
========

Setup
-----

### Step 1: Initial setup.

```basemake
./scripts/make_vmlinux_link.sh <path to your kernel's vmlinux file>
./scripts/make_qemu_img_link.sh <path to your QEMU VM image>
```

### Step 2: Compiling Granary.

If you are compiling Granary for user space, run:

```basemake
make clean ; make all
```

If you are compiling Granary against your running kernel, run:

```basemake
make clean ; make all GRANARY_WHERE=kernel
```

If you are compiling Granary against a custom kernel, run:

```basemake
make clean  GRANARY_WHERE=kernel
make all GRANARY_WHERE=kernel GRANARY_KERNEL_DIR=<path-to-kernel-source>
```

### Step 3: Compiling tools.

If you are compiling Granary tools against a custom kernel, run:

```basemake
make clean_tools GRANARY_WHERE=kernel
make tools GRANARY_TOOLS="follow_jumps count_bbs print_bbs" GRANARY_WHERE=kernel GRANARY_KERNEL_DIR=<path-to-kernel-source>
```